#!/usr/bin/env python3
"""
check_doc_refs.py — every "§N.M" a comment cites points at a section that exists.

The code cites the documentation constantly: a test explains what it pins with
"doc/3 §1.11 promises …", a header points at "the spec, §\"Version directive\"".
Those citations are the only thing tying a subtle test back to the sentence that
made it a requirement, and nothing checked them — so they rot in two directions.

Renumbering rots them wholesale. doc/3 numbered its function sections 1-25
straight through, was renumbered to N.M, and every "§7c" in the test suite
quietly began pointing at nothing; the documents' own links were all verified,
which is exactly why it went unnoticed. And they rot one at a time: a comment
cited "spec §501", which was never a section number, and another quoted a doc/2
heading — "An affine unit is valid at exponent 1 only" — that the document does
not contain, in either case describing a rule that is really there under a
number the reader cannot follow.

What a citation may look like, and how the target document is resolved:

    spec §7.4                 -> doc/1          (also "specification")
    doc/3 §1.11               -> doc/3          (a "doc/N" anywhere before it)
    read/write API §1.10      -> doc/3          (a name cue -- see CUES)
    §11 of [Unit Ambiguities] -> unit_ambiguities.md  (a cue right after it)
    §13.2                     -> the spec, from source; from inside doc/X, that
                                 same document -- which is what every bare "§"
                                 in the tree means today, and now the rule.
    §"Version directive"      -> a quoted name, matched case-insensitively
                                 against the document's text

A citation to an external standard (IEEE 754 §3.5.2, ISO 8601 §4.2) is left
alone: the skip list is what tells those apart from a citation into doc/.

Usage:
    python3 check_doc_refs.py            # report and exit 1 on a dead citation
    python3 check_doc_refs.py --verbose  # also list what resolved, and where
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
DOC_DIR = os.path.join(ROOT, "doc")

SEARCH_DIRS = ["include", "src", "python", "tests", "wasm", "examples",
               "highlighter", "cmake", "doc"]
SUFFIXES = (".c", ".h", ".py", ".sh", ".cmake", ".mjs", ".js", ".md", ".txt")
SKIP_DIRS = {"build", "__pycache__", ".git", ".pytest_cache", "node_modules"}

SPEC = "1_bovnar_spec.md"

# Name cues. Only phrases that can mean nothing but a document: "streaming",
# "conformance" and "FAQ" read as ordinary English in this tree and hijacked
# every bare "§" near them. Matched on word boundaries -- "spec" as a substring
# also lives inside "inspection", which is how a citation two words away from
# "partial inspection" resolved to the specification.
CUES = [
    (r"read\s*[/&]\s*write\s+api", "3_bovnar_readwrite_api.md"),
    (r"unit[- ]system\s+reference", "2_bovnar_unit_system.md"),
    (r"unit\s*&\s*currency\s+reference", "2_bovnar_unit_system.md"),
    (r"unit[_ ]ambiguities", "unit_ambiguities.md"),
    (r"cheat\s*sheet", "8_unit_cheatsheet.md"),
    (r"\bspecifications?\b", SPEC),
    (r"\bspec\b", SPEC),
]
# A markdown link into the doc set, "…](3_bovnar_readwrite_api.md#…)".
LINK = re.compile(r"\]\((?:doc/)?((?:\d_[\w]+|unit_ambiguities|datetime_[\w]+)\.md)")

# A "§" belonging to somebody else's document. Nothing in doc/ is numbered like
# these, so without the skip they would all read as dead citations into the spec.
EXTERNAL = re.compile(
    r"\b(IEEE|ISO|IEC|RFC|BIPM|SI Brochure|Unicode|POSIX|CommonMark|W3C|ECMA|"
    r"UTS|UAX|ITU|ANSI)\b", re.I)

DOCN = re.compile(r"doc/(\d)(?:_[\w.]+)?")
# "§5.3", "§7c", "§A.1", or §"a quoted section name"
REF = re.compile(r"§\s?(?:\"([^\"]{2,60})\"|([0-9]+(?:\.[0-9]+)*[a-z]?|[A-Z]\.[0-9]+))")

BY_NUMBER = {}          # doc filename -> {"5.3", "A.1", …}
TEXT = {}               # doc filename -> lowercased full text
FENCE = re.compile(r"^(```|~~~)")


def load_docs():
    for name in sorted(os.listdir(DOC_DIR)):
        if not name.endswith(".md"):
            continue
        text = open(os.path.join(DOC_DIR, name), encoding="utf-8").read()
        TEXT[name] = text.lower()
        nums, fence = set(), None
        for line in text.split("\n"):
            s = line.strip()
            m = FENCE.match(s)
            if m:
                fence = None if fence and s.startswith(fence) else (
                    fence or m.group(1))
                continue
            if fence:
                continue
            h = re.match(r"^#{2,4} +((?:[0-9]+(?:\.[0-9]+)*[a-z]?|[A-Z]\.[0-9]+))[. ]",
                         line)
            if h:
                nums.add(h.group(1).rstrip("."))
            ap = re.match(r"^## +Appendix ([A-Z])\b", line)
            if ap:
                nums.add(ap.group(1))
        BY_NUMBER[name] = nums
    if SPEC not in BY_NUMBER:
        raise SystemExit("check_doc_refs.py: doc/%s not found" % SPEC)


def _cue_doc(window):
    """The document a text window names, and how far into it the name sits."""
    best, pos = None, -1
    for pattern, doc in CUES:
        for m in re.finditer(pattern, window, re.I):
            if m.start() > pos:
                best, pos = doc, m.start()
    for m in LINK.finditer(window):
        if m.start() > pos and m.group(1) in BY_NUMBER:
            best, pos = m.group(1), m.start()
    for m in DOCN.finditer(window):
        if m.start() > pos:
            for name in BY_NUMBER:
                if name.startswith(m.group(1) + "_"):
                    best, pos = name, m.start()
    return best, pos


def resolve_doc(text, start, end, default):
    """Which document a citation at [start:end) is talking about.

    In order: the link it is written inside ("[FAQ §13 — …](6_bovnar_faq.md)"),
    the document named right after it ("§11 of the unit-system reference"), the
    nearest document named before it, and otherwise the default -- the spec from
    source, the containing document from inside doc/."""
    line_start = text.rfind("\n", 0, start) + 1
    line_end = text.find("\n", end)
    line = text[line_start:line_end if line_end != -1 else len(text)]
    off = start - line_start
    open_b = line.rfind("[", 0, off)
    close_b = line.find("](", off)
    if open_b != -1 and close_b != -1 and "]" not in line[open_b + 1:off]:
        m = LINK.match(line[close_b:])
        if m and m.group(1) in BY_NUMBER:
            return m.group(1)

    # "§3.26-3.27 of the unit-system reference": the range has to be stepped
    # over before the name that resolves both of its ends comes into view.
    after = re.sub(r"\s+", " ", text[end:end + 72])
    m = re.match(r"(?:[-\u2013\u2014]\s?[0-9.]+[a-z]?)? ?(?:of|in|,) (?:the )?[\[(`\"']*(.*)",
                 after)
    if m:
        doc, pos = _cue_doc(m.group(1)[:44])
        if doc is not None and pos <= 2:
            return doc

    # A short window on purpose. At seventy characters a document named in the
    # PREVIOUS clause -- "(§10.4 of the unit-system reference), array element
    # homogeneity (§7.4)" -- captured the next citation, which is the spec's.
    before = re.sub(r"\s+", " ", text[max(0, start - 34):start])
    doc, _pos = _cue_doc(before)
    return doc if doc is not None else default


def check_file(path, rel, verbose):
    text = open(path, encoding="utf-8", errors="replace").read()
    if "§" not in text:
        return []
    inside_doc = rel.startswith("doc/") and rel.endswith(".md")
    default = os.path.basename(rel) if inside_doc else SPEC
    if default not in BY_NUMBER:
        default = SPEC
    problems = []
    for m in REF.finditer(text):
        before = text[max(0, m.start() - 34):m.start()]
        if EXTERNAL.search(before):
            continue
        doc = resolve_doc(text, m.start(), m.end(), default)
        line = text.count("\n", 0, m.start()) + 1
        quoted, number = m.group(1), m.group(2)
        if quoted:
            if quoted.lower() not in TEXT[doc]:
                problems.append((line, f'§"{quoted}" is not text in doc/{doc}'))
            elif verbose:
                print(f'  ok  {rel}:{line}: §"{quoted}" -> doc/{doc}')
            continue
        if number not in BY_NUMBER[doc]:
            problems.append((line, f"§{number} is not a section of doc/{doc}"))
        elif verbose:
            print(f"  ok  {rel}:{line}: §{number} -> doc/{doc}")
    return problems


def main(argv):
    verbose = "--verbose" in argv
    load_docs()
    failed = total = 0
    for d in SEARCH_DIRS:
        base = os.path.join(ROOT, d)
        if not os.path.isdir(base):
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [x for x in dirnames if x not in SKIP_DIRS]
            for fn in sorted(filenames):
                if not fn.endswith(SUFFIXES):
                    continue
                path = os.path.join(dirpath, fn)
                rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
                problems = check_file(path, rel, verbose)
                total += 1
                for line, msg in problems:
                    failed += 1
                    print(f"{rel}:{line}: {msg}")
    if failed:
        print(f"\n{failed} citation(s) point at a section that does not exist.")
        print("Renumbering a document means carrying its citations over with "
              "it — see the header of check_doc_refs.py.")
        return 1
    print(f"check_doc_refs: every section citation across {total} files resolves.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
