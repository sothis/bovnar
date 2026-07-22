#!/usr/bin/env python3
"""Generate the translated Bovnar website from web/index.html + a translation table.

web/index.html is the single source of truth. This script never writes to it.
It produces web/<lang>/index.html (e.g. web/de/index.html) by splicing translated
text into an exact copy of the English page.

Translation units
-----------------
A unit is the innerHTML of the outermost element that contains text but no
block-level children -- so a heading, paragraph or list item is translated as a
whole, with its inline <em>/<code>/<br> markup included. That matters: German
reorders words across those inline tags, so translating the text runs between
them separately would produce broken grammar.

Units are keyed by their English source text, not by ids injected into the
markup. Two consequences, both deliberate:

  * index.html needs no i18n scaffolding at all, so the English page carries no
    cost for the translated ones and there is no attribute soup to maintain.
  * editing the English orphans its translation. The key simply stops matching,
    this script reports it as untranslated and exits non-zero. A stale
    translation can never be published silently -- the same guarantee
    cmake/wasm_freshness.cmake gives the WASM artifact.

Code is not translated. Text inside <script>, <style>, and the syntax-highlighted
code blocks is copied verbatim, except for comment spans, which are prose.

Usage
-----
  ./gen_i18n.py --extract de     rewrite web/i18n/de.json, preserving existing
                                 translations and adding new/changed units with
                                 empty values (never discards work silently)
  ./gen_i18n.py de               generate web/de/index.html; fails if any unit
                                 is untranslated
  ./gen_i18n.py --check de       verify only; write nothing (for CI / run_tests)
"""
from __future__ import annotations

import argparse
import html.parser
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
WEB = ROOT / "web"
SRC = WEB / "index.html"
I18N_DIR = WEB / "i18n"

# Elements that establish a new block context. A candidate unit that contains
# any of these is not a unit itself -- we recurse into it instead.
BLOCK = {
    "html", "body", "head", "div", "section", "article", "aside", "nav", "main",
    "header", "footer", "form", "fieldset", "table", "thead", "tbody", "tfoot",
    "tr", "ul", "ol", "dl", "figure", "details", "hgroup", "blockquote", "pre",
    "h1", "h2", "h3", "h4", "h5", "h6", "p", "li", "dt", "dd", "td", "th",
    "figcaption", "summary", "label", "button", "canvas", "textarea",
    "select", "option", "picture", "video", "audio", "template", "title",
}
# Never a unit even if the bookkeeping says they hold only inline content --
# their innerHTML is the whole document, not a sentence.
NEVER_UNIT = {"html", "head", "body"}
# <a> is deliberately not in BLOCK. It is phrasing content and usually sits
# mid-sentence ("...or <a>download the full set</a>."), where treating it as a
# block would strand the surrounding words outside every unit. When an <a>
# genuinely wraps block content (the doc cards), the has_block bookkeeping below
# disqualifies it anyway, so no special case is needed.
# Never descend into these -- their text is code or styling, not prose.
OPAQUE = {"script", "style"}
VOID = {
    "br", "hr", "img", "input", "meta", "link", "source", "area", "base",
    "col", "embed", "param", "track", "wbr",
}
# Attributes whose values reach a user or a screen reader.
TRANS_ATTRS = {"alt", "title", "placeholder", "aria-label"}
# <meta> whose content is prose, matched on name= or property=.
META_PROSE = re.compile(r"^(description|og:(title|description|image:alt)|twitter:(title|description|image:alt))$")

# Class names marking syntax-highlighted code. Text inside these is verbatim,
# except comments, which are prose the reader is meant to understand.
#
# Note the playground's pg-* classes are deliberately absent: in the static
# markup they are UI chrome (buttons, column titles, the error panel), not code
# tokens. The pg-* spans that *do* hold code are built at runtime inside
# <script>, which never reaches this matcher.
# Matched by PREFIX, not by enumerating every token name: the highlighter emits
# c-*, py-*, sh-* and cl-* spans (c-sigil, c-eq, c-ann, c-param, ...) and a list
# would silently rot as new ones appear -- a missed token makes its whole code
# block look like prose and get "translated". Every class in index.html carrying
# one of these prefixes is a token; no prose class does.
CODE_CLASS = re.compile(r"\b(?:(?:c|py|sh|cl)-[a-z]+|code-block|tok)\b")
COMMENT_CLASS = re.compile(r"\b(c-comment|py-comment|sh-comment|comment)\b")


def has_letters(s: str) -> bool:
    """Prose has at least one two-letter word; '42' and '×' do not."""
    return bool(re.search(r"[A-Za-zÄÖÜäöüß]{2}", s))


class UnitFinder(html.parser.HTMLParser):
    """Locate translation units and translatable attributes by byte offset.

    Works on offsets rather than a rebuilt tree so the generator can splice the
    original file: everything not translated stays byte-identical, which keeps
    the generated page reviewable as a diff against the English one.
    """

    def __init__(self, src: str):
        super().__init__(convert_charrefs=False)
        self.src = src
        self.lines = [0]
        for line in src.splitlines(keepends=True):
            self.lines.append(self.lines[-1] + len(line))
        # open elements: [tag, attrs, content_start_offset, has_block_child,
        #                 has_text, in_code, in_comment, has_code_descendant]
        self.stack: list[list] = []
        self.opaque_depth = 0
        # Every element that could be a unit. Elements close innermost-first,
        # so an <em> inside an <h1> is recorded first; find_units() then keeps
        # only the outermost of each nest, which is the whole <h1>.
        self.cand: list[tuple[int, int, str]] = []    # (start, end, innerHTML)
        self.attrs: list[tuple[int, int, str]] = []   # (start, end, value)
        self.text_nodes: list[tuple[int, int, str]] = []  # (offset, line, text)

    def _off(self) -> int:
        line, col = self.getpos()
        return self.lines[line - 1] + col

    def _end_of_tag(self) -> int:
        return self._off() + len(self.get_starttag_text() or "")

    def handle_starttag(self, tag, attrs):
        if self.opaque_depth:
            if tag in OPAQUE:
                self.opaque_depth += 1
            return
        if tag in OPAQUE:
            self.opaque_depth += 1
            for frame in self.stack:
                frame[3] = True
            return

        raw = self.get_starttag_text() or ""
        cls = dict(attrs).get("class", "") or ""
        parent_code = self.stack[-1][5] if self.stack else False
        parent_comment = self.stack[-1][6] if self.stack else False
        in_code = parent_code or bool(CODE_CLASS.search(cls))
        in_comment = parent_comment or bool(COMMENT_CLASS.search(cls))

        # Translatable attributes are independent of the unit structure.
        if not in_code or in_comment:
            self._collect_attrs(tag, attrs, raw)

        if tag in BLOCK or (in_code and not in_comment):
            # Mark every ancestor, not just the parent: a wrapper whose only
            # children are <a>s that each contain <div>s is still block-level,
            # and must not become one giant unit spanning all of them.
            #
            # Code spans count too. Their container (<div class="syntax-card-body">)
            # carries no token class of its own, so without this it would look
            # like an inline-only element and become a single unit holding an
            # entire highlighted code listing.
            for frame in self.stack:
                frame[3] = True
        if in_code and not in_comment:
            for frame in self.stack:
                frame[7] = True
        if tag in VOID:
            return
        self.stack.append([tag, dict(attrs), self._end_of_tag(), False, False,
                           in_code, in_comment, False])

    def _collect_attrs(self, tag, attrs, raw):
        """Record offsets of translatable attribute values inside a start tag."""
        wanted = []
        if tag == "meta":
            d = dict(attrs)
            name = d.get("name") or d.get("property") or ""
            if META_PROSE.match(name) and d.get("content"):
                wanted.append(("content", d["content"]))
        for a, v in attrs:
            if a in TRANS_ATTRS and v and has_letters(v):
                wanted.append((a, v))
        base = self._off()
        for name, value in wanted:
            # Find this attribute's value span within the raw start tag. Match
            # the attribute by name so repeated values can't be confused.
            m = re.search(
                rf"""\b{re.escape(name)}\s*=\s*(?:"([^"]*)"|'([^']*)')""", raw)
            if not m:
                continue
            g = 1 if m.group(1) is not None else 2
            self.attrs.append((base + m.start(g), base + m.end(g), m.group(g)))

    def handle_startendtag(self, tag, attrs):
        if self.opaque_depth:
            return
        raw = self.get_starttag_text() or ""
        cls = dict(attrs).get("class", "") or ""
        if not (self.stack and self.stack[-1][5]) or COMMENT_CLASS.search(cls):
            self._collect_attrs(tag, attrs, raw)
        if tag in BLOCK:
            for frame in self.stack:
                frame[3] = True

    def handle_endtag(self, tag):
        if self.opaque_depth:
            if tag in OPAQUE:
                self.opaque_depth -= 1
            return
        # Unwind to the matching open tag (the document has a few unclosed
        # inline elements; tolerate them rather than desynchronising).
        for i in range(len(self.stack) - 1, -1, -1):
            if self.stack[i][0] == tag:
                frame = self.stack[i]
                del self.stack[i:]
                break
        else:
            return

        name, _attrs, start, has_block, has_text, in_code, in_comment, _hc = frame
        end = self._off()
        if not has_text or has_block or name in NEVER_UNIT:
            return
        if in_code and not in_comment:
            return
        inner = self.src[start:end]
        if not has_letters(inner):
            return
        self.cand.append((start, end, inner))

    def handle_data(self, data):
        if self.opaque_depth or not self.stack:
            return
        if not data.strip() or not has_letters(data):
            return
        # Mark every open ancestor, not just the innermost: in
        # "<h1><em>x</em></h1>" the <h1> holds no direct text, but it is still
        # the right unit to translate as a whole.
        for frame in self.stack:
            frame[4] = True
        top = self.stack[-1]
        if not (top[5] and not top[6]):
            # Keep a reference to the enclosing frame: has_code_descendant may
            # only be set later, when a token span further along is opened.
            self.text_nodes.append(
                (self._off(), self.getpos()[0], data.strip(), top))


def find_units(src: str):
    """Return (units, attrs, uncovered).

    A unit is the OUTERMOST element that holds text and no block-level child.
    Elements close innermost-first, so candidates arrive inside-out; keeping the
    outermost of each nest is what makes "<h1>Unit-safe<br>for <em>science</em>"
    one translatable sentence instead of three fragments that German cannot
    reorder.

    `uncovered` lists prose the extractor did not place in any unit -- text
    stranded beside block children. It must stay empty, or the generated page
    would silently keep that text in English.
    """
    p = UnitFinder(src)
    p.feed(src)

    units = []
    last_end = -1
    for start, end, inner in sorted(p.cand, key=lambda c: (c[0], -c[1])):
        if start < last_end:
            continue                    # nested inside the unit already kept
        units.append((start, end, inner))
        last_end = end

    # An attribute sitting inside a unit is already part of that unit's
    # innerHTML and is translated with it. Emitting it as a separate edit too
    # would rewrite the same bytes twice and corrupt the output, so drop it.
    attrs = [a for a in p.attrs
             if not any(s <= a[0] and a[1] <= e for s, e, _ in units)]

    uncovered = [(line, t) for off, line, t, frame in p.text_nodes
                 if not frame[7]                       # loose text inside code
                 and not any(s <= off < e for s, e, _ in units)]
    return units, attrs, uncovered


def load_table(lang: str) -> dict:
    path = I18N_DIR / f"{lang}.json"
    if not path.exists():
        return {"_meta": {"lang": lang}, "units": {}, "attrs": {}, "js": {}}
    return json.loads(path.read_text(encoding="utf-8"))


def cmd_extract(lang: str) -> int:
    src = SRC.read_text(encoding="utf-8")
    units, attrs, uncovered = find_units(src)
    table = load_table(lang)
    old_u, old_a = table.get("units", {}), table.get("attrs", {})

    new_u, new_a = {}, {}
    for _s, _e, inner in units:
        key = inner.strip()
        new_u[key] = old_u.get(key, "")
    for _s, _e, value in attrs:
        new_a[value] = old_a.get(value, "")

    dropped = [k for k in old_u if k not in new_u and old_u[k]]
    dropped += [k for k in old_a if k not in new_a and old_a[k]]
    table["units"], table["attrs"] = new_u, new_a
    table.setdefault("js", {})
    table.setdefault("_meta", {})["lang"] = lang
    # Keep translations that no longer match, under a separate key, so hand
    # work is never destroyed by an extract after an English edit.
    if dropped:
        stale = table.setdefault("_orphaned", {})
        for k in dropped:
            stale[k] = old_u.get(k) or old_a.get(k)

    I18N_DIR.mkdir(parents=True, exist_ok=True)
    (I18N_DIR / f"{lang}.json").write_text(
        json.dumps(table, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    todo_u = sum(1 for v in new_u.values() if not v)
    todo_a = sum(1 for v in new_a.values() if not v)
    print(f"{len(new_u)} units ({todo_u} untranslated), "
          f"{len(new_a)} attributes ({todo_a} untranslated)")
    if dropped:
        print(f"{len(dropped)} translation(s) no longer match the English "
              f"source; moved to _orphaned in {lang}.json")
    return report_uncovered(uncovered)


def report_uncovered(uncovered) -> int:
    """Prose that belongs to no unit would silently ship untranslated."""
    if not uncovered:
        return 0
    print(f"\nerror: {len(uncovered)} prose string(s) belong to no translation "
          f"unit and would stay English:", file=sys.stderr)
    for line, text in uncovered[:20]:
        print(f"  index.html:{line}: {text[:90]}", file=sys.stderr)
    if len(uncovered) > 20:
        print(f"  ... and {len(uncovered) - 20} more", file=sys.stderr)
    return 1


def generate(lang: str, check_only: bool) -> int:
    src = SRC.read_text(encoding="utf-8")
    units, attrs, uncovered = find_units(src)
    table = load_table(lang)
    tu, ta = table.get("units", {}), table.get("attrs", {})

    missing = []
    edits = []   # (start, end, replacement)
    for s, e, inner in units:
        key = inner.strip()
        val = tu.get(key)
        if not val:
            missing.append(key)
            continue
        lead = inner[:len(inner) - len(inner.lstrip())]
        tail = inner[len(inner.rstrip()):]
        edits.append((s, e, lead + val + tail))
    for s, e, value in attrs:
        val = ta.get(value)
        if not val:
            missing.append(value)
            continue
        edits.append((s, e, val))

    if missing:
        print(f"error: {len(missing)} untranslated string(s) for '{lang}'.",
              file=sys.stderr)
        for m in missing[:15]:
            print(f"  - {m[:110]}", file=sys.stderr)
        if len(missing) > 15:
            print(f"  ... and {len(missing) - 15} more", file=sys.stderr)
        print(f"Run: ./gen_i18n.py --extract {lang}   then fill in "
              f"web/i18n/{lang}.json", file=sys.stderr)
        return 1

    bad = check_js_paths(src)
    if bad:
        print(f"error: {len(bad)} document-relative path(s) in <script> do not "
              f"use BASE; they would 404 under web/{lang}/:", file=sys.stderr)
        for b in bad:
            print(f"  {b}", file=sys.stderr)
        return 1

    if check_only:
        print(f"{lang}: all {len(units)} units and {len(attrs)} attributes "
              f"are translated; script paths use BASE")
        return 0

    out = []
    pos = 0
    for s, e, rep in sorted(edits):
        # Overlapping spans would splice the same bytes twice and quietly
        # corrupt the page. find_units() already excludes attributes nested in
        # a unit; anything else overlapping is a bug, so refuse to write.
        if s < pos:
            raise AssertionError(
                f"overlapping edit at offset {s} (previous ended at {pos}): "
                f"{src[s:e][:80]!r}")
        out.append(src[pos:s])
        out.append(rep)
        pos = e
    out.append(src[pos:])
    doc = "".join(out)
    doc = localize_document(doc, lang, table)

    dest_dir = WEB / lang
    dest_dir.mkdir(parents=True, exist_ok=True)
    (dest_dir / "index.html").write_text(doc, encoding="utf-8")
    print(f"wrote web/{lang}/index.html "
          f"({len(units)} units, {len(attrs)} attributes)")
    return 0


def localize_document(doc: str, lang: str, table: dict) -> str:
    """Fix up document-level metadata that is not a translation unit."""
    locale = {"de": "de_DE"}.get(lang, lang)
    doc = doc.replace('<html lang="en">', f'<html lang="{lang}">', 1)
    doc = re.sub(r'(<meta property="og:locale" content=")[^"]*(")',
                 rf"\g<1>{locale}\g<2>", doc, count=1)
    # Assets and links are one directory deeper under web/<lang>/.
    doc = re.sub(r'((?:href|src)=")(?!https?:|//|#|/|data:)', r"\1../", doc)
    # This edition is its own canonical URL. Left pointing at the English page,
    # it would tell search engines the translation is a duplicate and should not
    # be indexed at all -- exactly the opposite of why it exists. The hreflang
    # alternates are absolute and deliberately copied unchanged.
    doc = re.sub(r'(<link rel="canonical" href="https://[^"]*?)/?"',
                 rf'\g<1>/{lang}/"', doc, count=1)
    doc = re.sub(r'(<meta property="og:url" content="https://[^"]*?)/?"',
                 rf'\g<1>/{lang}/"', doc, count=1)
    # The translated page lives one directory down, so paths that JavaScript
    # resolves against the document need a prefix. index.html reads this as
    # BASE; check_js_paths() guarantees nothing bypasses it.
    boot = {"__BVNR_BASE__": "../"}
    js = table.get("js", {})
    if js:
        boot["__BVNR_I18N__"] = js
    decls = "".join(f"window.{k}={json.dumps(v, ensure_ascii=False)};"
                    for k, v in boot.items())
    doc = doc.replace("</head>", f"<script>{decls}</script>\n</head>", 1)
    return doc


# Quoted paths in <script> that resolve against the document, not the script.
JS_PATH = re.compile(
    r"""(?P<pre>.{0,12})(?P<q>['"])(?P<path>(?!https?:|//|/|\#|data:|\.\./)"""
    r"""[\w][\w./-]*(?:\.(?:png|jpe?g|svg|gif|webp|ico|css|js|mjs|json|md|pdf|zip)|/))"""
    r"""(?P=q)""")


def check_js_paths(src: str) -> list[str]:
    """Find document-relative asset paths in <script> that ignore BASE.

    Such a path silently 404s on every translated page -- it resolves against
    /de/ instead of the site root -- and nothing else would catch it, because
    the English page it was written against works fine.
    """
    bad = []
    for m in re.finditer(r"<script\b[^>]*>(.*?)</script>", src,
                         re.S | re.I):
        block, base = m.group(1), m.start(1)
        for p in JS_PATH.finditer(block):
            if "BASE +" in p.group("pre") or "__BVNR" in p.group("pre"):
                continue
            line = src.count("\n", 0, base + p.start("path")) + 1
            bad.append(f"index.html:{line}: {p.group('path')}")
    return bad


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("lang", help="language code, e.g. de")
    ap.add_argument("--extract", action="store_true",
                    help="refresh the translation table from index.html")
    ap.add_argument("--check", action="store_true",
                    help="verify every string is translated; write nothing")
    a = ap.parse_args()
    if not SRC.exists():
        print(f"error: {SRC} not found", file=sys.stderr)
        return 2
    if a.extract:
        return cmd_extract(a.lang)
    return generate(a.lang, a.check)


if __name__ == "__main__":
    sys.exit(main())
