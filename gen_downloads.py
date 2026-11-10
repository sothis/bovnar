#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""Generate web/download/index.html — the all-releases downloads page.

The Downloads menu serves the newest release's archives from /download/<version>/
and its "All releases" entry points here. This page lists every release fetched
into build/releases/ (fetch_release_artifacts.py), newest first, with each
archive's size and its SHA256SUMS link. The release directory is git-ignored,
so a checkout without the archives cannot rebuild the page: --check says so and
leaves the committed copy alone rather than failing, exactly as the PDFs are
handled.

publish_web.sh runs this after the docs pages and before the sitemap. Run:
    python3 gen_downloads.py            # write web/download/index.html
    python3 gen_downloads.py --check    # verify it is current
"""
import html
import os
import re
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ROOT)

from gen_html_docs import SITE, page  # noqa: E402

RELEASES = os.path.join(ROOT, "build", "releases")
OUT = os.path.join(ROOT, "web", "download", "index.html")

# The order the menu lists the same files in; unknown names sort after these.
ASSETS = [
    (re.compile(r"^bovnar-linux-.*\.tar\.xz$"), "Linux x64"),
    (re.compile(r"^bovnar-freebsd-.*\.tar\.xz$"), "FreeBSD x64"),
    (re.compile(r"^bovnar-macos-.*\.tar\.xz$"), "macOS · arm64 + x86_64"),
    (re.compile(r"^bovnar-windows-msvc-.*\.zip$"), "Windows x64 · MSVC"),
    (re.compile(r"^bovnar-windows-mingw-.*\.zip$"), "Windows x64 · MinGW"),
    (re.compile(r"^bovnar-.*-amalgamate\.tar\.xz$"), "Single-file amalgamation"),
    (re.compile(r"^bovnar-.*-source\.tar\.xz$"), "Source tarball"),
    (re.compile(r"^SHA256SUMS$"), "SHA256SUMS"),
]


def label(name):
    for pattern, text in ASSETS:
        if pattern.match(name):
            return text
    return name


def order(name):
    for i, (pattern, _) in enumerate(ASSETS):
        if pattern.match(name):
            return i
    return len(ASSETS)


def human(n):
    return f"{n / 1048576:.1f} MB"


def versions():
    if not os.path.isdir(RELEASES):
        return []
    found = [d for d in os.listdir(RELEASES)
             if re.fullmatch(r"\d+\.\d+\.\d+", d)
             and os.path.isdir(os.path.join(RELEASES, d))]
    return sorted(found, key=lambda v: tuple(int(p) for p in v.split(".")),
                  reverse=True)


def build():
    vs = versions()
    if not vs:
        return None
    items = []
    for v in vs:
        vdir = os.path.join(RELEASES, v)
        files = sorted((f for f in os.listdir(vdir)
                        if os.path.isfile(os.path.join(vdir, f))),
                       key=lambda n: (order(n), n))
        links = []
        for f in files:
            href = f"{SITE}/download/{v}/{f}"
            text = html.escape(label(f))
            if f == "SHA256SUMS":
                links.append(f'<a href="{href}">{text}</a>')
            else:
                size = human(os.path.getsize(os.path.join(vdir, f)))
                links.append(f'<a href="{href}">{text}</a> ({size})')
        items.append(
            "<li><strong>Bovnar " + html.escape(v) + "</strong>"
            '<div class="d">Archives built by the release job for this version; '
            "each file name carries the version and the release's commit count. "
            "SHA256SUMS lists every archive.</div>"
            '<div class="fmt">' + " · ".join(links) + "</div></li>")
    body = (
        "<h1>Downloads</h1>\n"
        '<p class="doc-meta">Release archives of Bovnar (BVNR), built by the '
        "release CI and served from this host. Download the archives and "
        "SHA256SUMS into one directory, then verify them with "
        "<code>sha256sum -c SHA256SUMS</code>.</p>\n"
        '<ul class="doclist">\n' + "\n".join(items) + "\n</ul>\n")
    return page(
        "Bovnar Downloads",
        "Release archives of Bovnar (BVNR), served from this host: Linux, "
        "FreeBSD, macOS and Windows builds, the amalgamation, the source "
        "tarball, and SHA256 checksums.",
        f"{SITE}/download/", body)


def main():
    check = "--check" in sys.argv[1:]
    out = build()
    if out is None:
        print("gen_downloads.py: no release archives under build/releases/ — "
              "leaving web/download/index.html unchanged; run "
              "fetch_release_artifacts.py to populate it")
        return 0
    if check:
        cur = (open(OUT, encoding="utf-8").read()
               if os.path.isfile(OUT) else None)
        if cur != out:
            print("gen_downloads.py --check: web/download/index.html is stale; "
                  "run python3 gen_downloads.py", file=sys.stderr)
            return 1
        print("gen_downloads.py --check: web/download/index.html is current")
        return 0
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write(out)
    print(f"==> Wrote web/download/index.html ({len(versions())} release(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
