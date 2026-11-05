#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""Tell IndexNow search engines (Bing, Yandex, Seznam, Naver, ...) that the
site's pages changed, right after publish_web.sh has uploaded them.

Bing Webmaster Tools ranked the missing IndexNow setup as the site's one
high-severity issue, and Bing's Site Explorer showed most pages last crawled
weeks before the content it was serving had changed. One POST to
api.indexnow.org reaches every participating engine.

The protocol proves ownership with a key file at the site root whose name and
content are the key: web/<32 hex>.txt. It is public by design -- anyone can read
it, and all it allows is asking for a recrawl of this host's URLs -- so it is
committed like any other page. Exactly one such file must exist.

What is submitted is the sitemap: the canonical pages, nothing else. The PDFs
and raw Markdown are left out for the reason the sitemap leaves them out.

  python3 indexnow_ping.py            submit (publish_web.sh runs this)
  python3 indexnow_ping.py --dry-run  print what would be sent
"""
import json
import pathlib
import re
import sys
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent
WEB = ROOT / "web"
HOST = "www.bovnar.io"
ENDPOINT = "https://api.indexnow.org/indexnow"


def key():
    keys = [p for p in WEB.glob("*.txt")
            if re.fullmatch(r"[0-9a-f]{32}", p.stem)
            and p.read_text().strip() == p.stem]
    if len(keys) != 1:
        sys.exit(f"indexnow_ping: expected one key file web/<key>.txt, found "
                 f"{len(keys)}")
    return keys[0].stem


def urls():
    sitemap = (WEB / "sitemap.xml").read_text(encoding="utf-8")
    locs = re.findall(r"<loc>([^<]+)</loc>", sitemap)
    if not locs:
        sys.exit("indexnow_ping: web/sitemap.xml lists no URLs")
    return locs


def main():
    k = key()
    body = {"host": HOST, "key": k,
            "keyLocation": f"https://{HOST}/{k}.txt", "urlList": urls()}
    if "--dry-run" in sys.argv[1:]:
        print(json.dumps(body, indent=2))
        return 0
    req = urllib.request.Request(
        ENDPOINT, data=json.dumps(body).encode(), method="POST",
        headers={"Content-Type": "application/json; charset=utf-8",
                 "User-Agent": "bovnar-publish (indexnow_ping.py)"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            status = r.status
    except urllib.error.HTTPError as e:
        status = e.code
    except OSError as e:
        print(f"indexnow_ping: request failed: {e}", file=sys.stderr)
        return 1
    # 200 = accepted, 202 = accepted, key validation pending. 403 means the key
    # file was not found at keyLocation (not uploaded yet?), 422 that a URL is
    # not on HOST, 429 too many requests.
    ok = status in (200, 202)
    print(f"indexnow_ping: {len(body['urlList'])} URL(s) submitted, HTTP {status}"
          + ("" if ok else " (not accepted)"),
          file=sys.stdout if ok else sys.stderr)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
