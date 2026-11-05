#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""Content-stamp the webfonts, and the stylesheet that reaches them.

The server hands out `public, max-age=31536000, immutable` to any asset asked
for with a 12-hex `?v=` stamp, and one hour to everything else (the
$bvnr_cache_ctl map). Every versioned asset on the site earns that year -- except
the fonts, which are reached only from `url()` inside web/fonts/fonts.css and
carried no stamp at all. So ~440 kB of woff2 that has not changed in months
revalidated hourly, on the page's critical path, for every returning visitor.

Stamping them by hand is exactly the failure this repo has already had once:
web/impressum.html still asks for `fonts/fonts.css?v=20260724`, a date somebody
was supposed to remember to bump, which the map deliberately refuses to treat as
immutable. So the stamps are derived here, from the bytes:

  * each url(...woff2) inside fonts.css gets that font file's hash
  * every page that links fonts.css gets the (now rewritten) stylesheet's hash
  * the orbit demo's two textures on the landing page get theirs (TEXTURES)

  ./gen_font_stamps.py           rewrite the stamps in place
  ./gen_font_stamps.py --check   verify only; exit non-zero on drift (CI)
"""
from __future__ import annotations

import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
WEB = ROOT / "web"
FONTS_CSS = WEB / "fonts" / "fonts.css"
# Pages that link the stylesheet. Both spell the href relative to themselves.
PAGES = [WEB / "index.html", WEB / "impressum.html"]

FONT_URL = re.compile(r"""url\((['"]?)([\w.-]+\.woff2)(\?v=[0-9a-f]+)?\1\)""")
CSS_LINK = re.compile(r"""(href="[^"]*fonts/fonts\.css)(\?v=[0-9a-f]+)?(")""")
# <link rel="preload" as="font" href="fonts/<name>.woff2"> on the landing page.
# A preload only satisfies a later request for the SAME URL, so the moment the
# stylesheet started asking for a stamped one, an unstamped preload downloaded
# every face a second time -- exactly what the modulepreload comment beside it
# warns about for the WASM chain.
FONT_PRELOAD = re.compile(
    r"""(href="[^"]*fonts/)([\w.-]+\.woff2)(\?v=[0-9a-f]+)?(")""")


# The orbit demo's textures. They went out as bare earth.jpg / earth_night.jpg,
# so they got the one-hour default while everything else on the landing page is
# a year-long immutable URL; PageSpeed Insights listed their ~120 kB as the
# page's only short-lived cache entries. Each is named twice on the page -- the
# <link rel=preload> and the script's new Image() -- and both must carry the
# same stamp, or the preloaded copy is not reused and the file downloads twice.
# The pattern requires a closing quote, so the names in prose comments are left
# alone. (This lives here, not in wasm/build_wasm.sh with the module stamps:
# that script is itself an input to the WASM freshness hash.)
TEXTURES = ["earth.jpg", "earth_night.jpg"]
TEXTURE_PAGE = WEB / "index.html"


def texture_re(name: str):
    return re.compile(r"(" + re.escape(name) + r""")(\?v=[0-9a-zA-Z]+)?(["'])""")


def stamp(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()[:12]


def rewrite_css(text: str) -> tuple[str, list[str]]:
    """Return (new css, missing font files)."""
    missing: list[str] = []

    def repl(m):
        quote, name = m.group(1), m.group(2)
        font = FONTS_CSS.parent / name
        if not font.exists():
            missing.append(name)
            return m.group(0)
        return f"url({quote}{name}?v={stamp(font)}{quote})"

    return FONT_URL.sub(repl, text), missing


def main() -> int:
    check = "--check" in sys.argv[1:]

    css = FONTS_CSS.read_text(encoding="utf-8")
    new_css, missing = rewrite_css(css)
    if missing:
        print("gen_font_stamps.py: fonts.css references missing file(s): "
              + ", ".join(sorted(set(missing))), file=sys.stderr)
        return 1

    # The pages' stamp is the hash of the stylesheet AFTER its own rewrite --
    # stamping the fonts changes fonts.css, which changes what the pages must ask
    # for. Doing it in the other order ships a stylesheet under a stale URL.
    if check:
        digest = hashlib.sha256(new_css.encode()).hexdigest()[:12]
    else:
        if new_css != css:
            FONTS_CSS.write_text(new_css, encoding="utf-8")
        digest = stamp(FONTS_CSS)

    stale = []
    if new_css != css:
        stale.append("web/fonts/fonts.css")

    preloads = 0
    for page in PAGES:
        text = page.read_text(encoding="utf-8")
        if not CSS_LINK.search(text):
            print(f"gen_font_stamps.py: no fonts.css link in {page.name} -- "
                  f"drop it from PAGES or restore the link", file=sys.stderr)
            return 1
        new = CSS_LINK.sub(lambda m: f"{m.group(1)}?v={digest}{m.group(3)}", text)

        def preload(m):
            nonlocal preloads
            font = FONTS_CSS.parent / m.group(2)
            if not font.exists():
                missing.append(m.group(2))
                return m.group(0)
            preloads += 1
            return f"{m.group(1)}{m.group(2)}?v={stamp(font)}{m.group(4)}"

        new = FONT_PRELOAD.sub(preload, new)
        # A preload only pays off if the stylesheet asks for that exact URL. The
        # stamps match by construction (same bytes, same hash), but a face the
        # CSS no longer references would still be fetched -- eagerly, on the
        # critical path, and used by nothing.
        wanted = {m.group(2) for m in FONT_URL.finditer(new_css)}
        orphan = sorted({m.group(2) for m in FONT_PRELOAD.finditer(new)} - wanted)
        if orphan:
            print(f"gen_font_stamps.py: {page.name} preloads face(s) fonts.css "
                  f"never requests: {', '.join(orphan)}", file=sys.stderr)
            return 1
        if missing:
            print(f"gen_font_stamps.py: {page.name} preloads missing file(s): "
                  + ", ".join(sorted(set(missing))), file=sys.stderr)
            return 1
        if new != text:
            stale.append("web/" + page.name)
            if not check:
                page.write_text(new, encoding="utf-8")

    text = TEXTURE_PAGE.read_text(encoding="utf-8")
    new = text
    for name in TEXTURES:
        h = stamp(WEB / name)
        new, n = texture_re(name).subn(lambda m: f"{m.group(1)}?v={h}{m.group(3)}", new)
        if n != 2:
            print(f"gen_font_stamps.py: expected 2 references to {name} in "
                  f"{TEXTURE_PAGE.name} (preload + script), found {n}",
                  file=sys.stderr)
            return 1
    if new != text:
        if "web/" + TEXTURE_PAGE.name not in stale:
            stale.append("web/" + TEXTURE_PAGE.name)
        if not check:
            TEXTURE_PAGE.write_text(new, encoding="utf-8")

    if check:
        if stale:
            print("gen_font_stamps.py --check: stale stamps in "
                  + ", ".join(stale) + "\nRe-run:  ./gen_font_stamps.py",
                  file=sys.stderr)
            return 1
        n = len(FONT_URL.findall(css))
        print(f"gen_font_stamps.py --check: {n} font stamps, {preloads} preloads, "
              f"{len(PAGES)} stylesheet links and {len(TEXTURES)} textures are current")
        return 0

    print(f"gen_font_stamps.py: fonts.css -> ?v={digest}"
          + (f" (updated {', '.join(stale)})" if stale else " (already current)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
