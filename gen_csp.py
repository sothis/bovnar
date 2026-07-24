#!/usr/bin/env python3
"""Compute and stamp a hash-based Content-Security-Policy into the served pages.

The Bovnar site is static and self-hosted, with no server-side templating, so a
per-request nonce is impossible. Every inline <script> is therefore pinned by the
SHA-256 of its own bytes: the CSP lists 'sha256-...' for each, and the browser
recomputes and compares. No 'unsafe-inline' for scripts — an injected inline
script (or an onerror handler smuggled through the Markdown the doc drawer
renders into innerHTML) has no matching hash and does not run.

The hashes change whenever an inline script is edited, so this file is the tool
that recomputes them and cmake/csp.cmake (via --check) fails the suite when the
stamped policy no longer matches the scripts it is meant to authorise — the same
anti-drift discipline the ?v= cache stamps and the WASM artifact already have.

  ./gen_csp.py           rewrite the CSP <meta> in index.html, impressum.html, 404.html
  ./gen_csp.py --check   verify only; exit non-zero on drift

The translated editions carry ONE extra inline script (the __BVNR_I18N__ boot
blob gen_i18n.py injects). gen_i18n.py imports script_hashes()/build_meta() from
here and adds that hash when it writes web/<lang>/index.html, so the tooling for
the policy lives in one place.
"""
from __future__ import annotations

import base64
import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
WEB = ROOT / "web"

# A <script> element, capturing its attributes and its exact byte content.
# An attribute value never contains '>' on these pages, so [^>]* is safe; the
# content is captured verbatim (no entity decoding) because that is what the
# browser hashes.
SCRIPT_RE = re.compile(r"<script\b([^>]*)>(.*?)</script>", re.S)
# type= values the browser does NOT execute, so CSP never checks them.
DATA_TYPES = re.compile(r'type\s*=\s*["\']?(application/(ld\+)?json)', re.I)
HAS_SRC = re.compile(r'\bsrc\s*=', re.I)

CSP_META_RE = re.compile(
    r'<meta http-equiv="Content-Security-Policy" content="[^"]*">\n?')

# Directives that never change. default-src 'none' denies anything not named
# below; the exceptions (base-uri, form-action) do not fall back to it, so they
# are stated. frame-ancestors is intentionally absent: it is ignored in a <meta>
# policy and only warns -- real clickjacking protection needs an X-Frame-Options
# / frame-ancestors *header*, which is server config this repo cannot set.
STATIC_DIRECTIVES = [
    "default-src 'none'",
    # 'wasm-unsafe-eval' lets the emscripten parser compile its WebAssembly
    # without opening the door to string eval; 'self' covers the dynamically
    # injected marked/highlight.js and the import() of the WASM glue.
    "script-src 'self' 'wasm-unsafe-eval' {script_hashes}",
    # 'unsafe-inline' for STYLE only, and deliberately. The page has two inline
    # <style> blocks, ~21 style="" attributes, and a <style> the highlighter
    # creates at runtime and fills with the token palette; pinning all of those
    # by hash is impractical (attributes need 'unsafe-hashes', the runtime block
    # cannot be hashed ahead of time) and style injection is a far weaker vector
    # than script. Scripts carry the strict policy; styles do not.
    "style-src 'self' 'unsafe-inline'",
    "img-src 'self' data:",          # data: for the inline SVG favicon
    "font-src 'self'",
    "connect-src 'self'",            # the doc drawer's XHR to doc/*.md
    "manifest-src 'self'",
    "base-uri 'none'",
    "form-action 'none'",
]

# 404.html is a standalone card: one inline theme-boot script, no WASM, no fetch,
# no manifest, no webfont. It gets its own reduced policy rather than the app one
# -- the page has no use for 'wasm-unsafe-eval' or connect-src, and a policy is
# only worth having if it stays as narrow as the page it guards.
STANDALONE_DIRECTIVES = [
    "default-src 'none'",
    "script-src {script_hashes}",
    "style-src 'self' 'unsafe-inline'",
    "img-src 'self' data:",
    "font-src 'self'",
    "base-uri 'none'",
    "form-action 'none'",
]

# The pages served directly, each with the directive set it is stamped from. The
# translated editions are not here: gen_i18n.py stamps those through stamp().
PAGES = [
    (WEB / "index.html", STATIC_DIRECTIVES),
    (WEB / "impressum.html", STATIC_DIRECTIVES),
    (WEB / "404.html", STANDALONE_DIRECTIVES),
]


def sha256_b64(text: str) -> str:
    return base64.b64encode(hashlib.sha256(text.encode("utf-8")).digest()).decode()


def script_hashes(html: str) -> list[str]:
    """'sha256-...' for every executable inline <script> in the page, in order."""
    out = []
    for m in SCRIPT_RE.finditer(html):
        attrs, body = m.group(1), m.group(2)
        if HAS_SRC.search(attrs) or DATA_TYPES.search(attrs):
            continue
        out.append("'sha256-" + sha256_b64(body) + "'")
    return out


def build_meta(hashes: list[str], directives: list[str] | None = None) -> str:
    directives = [
        d.format(script_hashes=" ".join(hashes)) if "{script_hashes}" in d else d
        for d in (directives or STATIC_DIRECTIVES)
    ]
    return ('<meta http-equiv="Content-Security-Policy" content="'
            + "; ".join(directives) + '">')


def policy_for(html: str, extra_hashes: list[str] | None = None,
               directives: list[str] | None = None) -> str:
    hashes = script_hashes(html)
    if extra_hashes:
        hashes += extra_hashes
    return build_meta(hashes, directives)


def _stamp(html: str, meta: str) -> str:
    """Insert or replace the CSP meta, as the first thing after <meta charset>."""
    html = CSP_META_RE.sub("", html, count=1)
    # Placed right after charset (and before the first <script>) so it governs
    # every script, including the pre-paint theme/language boot scripts.
    anchor = re.search(r'(<meta charset="[^"]*">\n)', html)
    if not anchor:
        raise SystemExit("gen_csp.py: no <meta charset> to anchor the policy to")
    at = anchor.end()
    return html[:at] + meta + "\n" + html[at:]


def stamp(html: str, extra_hashes: list[str] | None = None) -> str:
    """Return html with its CSP <meta> recomputed from its own inline scripts.

    Used by gen_i18n.py on the final translated page, whose boot script is one
    more inline <script> than the English source has -- computing the policy from
    the finished bytes means that hash is included with no bookkeeping."""
    return _stamp(html, policy_for(html, extra_hashes))


def process(path: Path, check: bool, directives: list[str] | None = None) -> int:
    html = path.read_text(encoding="utf-8")
    # hashes are of the scripts as they are now
    meta = policy_for(html, directives=directives)
    want = _stamp(html, meta)
    if check:
        if html != want:
            cur = CSP_META_RE.search(html)
            print(f"error: {path.relative_to(ROOT)} has a stale or missing CSP.",
                  file=sys.stderr)
            print("  run ./gen_csp.py to restamp it.", file=sys.stderr)
            if cur:
                print(f"  stamped: {cur.group(0).strip()[:90]}...", file=sys.stderr)
            return 1
        print(f"{path.relative_to(ROOT)}: CSP matches "
              f"{len(script_hashes(html))} inline script(s)")
        return 0
    if html != want:
        path.write_text(want, encoding="utf-8")
        print(f"stamped {path.relative_to(ROOT)} "
              f"({len(script_hashes(want))} script hashes)")
    else:
        print(f"{path.relative_to(ROOT)}: already current")
    return 0


def main() -> int:
    check = "--check" in sys.argv[1:]
    rc = 0
    for page, directives in PAGES:
        if page.exists():
            rc |= process(page, check, directives)
    return rc


if __name__ == "__main__":
    sys.exit(main())
