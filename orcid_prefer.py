#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""
orcid_prefer.py — make a release the ORCID preferred source.

ORCID groups works by their common identifier and displays the item with the
highest display-index; the record owner raises one with "Make preferred
source" in the record UI. The public API cannot do it: a work can only be
updated by the client that created it, and the works here come from DataCite's
auto-update. The UI itself calls

    GET https://orcid.org/works/updateToMaxDisplay.json?putCode=<put-code>

with the browser session, and that is what this script calls, after finding
the new release's works through the public API.

The cookie file (default ~/.config/bovnar-orcid.cookie) holds either the
Cookie request header copied from a signed-in orcid.org browser session
(`name=value; ...`) or a Netscape cookies.txt export. It is a session
credential for the whole ORCID record: keep it mode 0600. When the session
expires the update is refused with a sign-in redirect; copy a fresh header
into the file and rerun.

Usage:
  ./orcid_prefer.py --tag v2.1.0 --check
  ./orcid_prefer.py --tag v2.1.0 --dry-run
  ./orcid_prefer.py --tag v2.1.0 --wait 600
  ./orcid_prefer.py --tag v2.1.0 --source-doi 10.5281/zenodo... \
      --docs-doi 10.5281/zenodo...

--check needs no cookie and exits 0 when both target works are preferred,
1 when either is not. A run after a Zenodo deposit can pass --wait to poll
until DataCite's auto-update has put the works on the record.
"""

from __future__ import annotations

import argparse
import http.client
import subprocess
import sys
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DEFAULT_COOKIE = Path.home() / ".config" / "bovnar-orcid.cookie"
PUBLIC_API = "https://pub.orcid.org/v3.0"
WEB_HOST = "orcid.org"
USER_AGENT = "bovnar-orcid-prefer/1.0"
NS = {
    "work": "http://www.orcid.org/ns/work",
    "common": "http://www.orcid.org/ns/common",
}
KINDS = ("source", "docs")


def fail(*lines: str) -> int:
    sys.stdout.flush()
    for line in lines:
        print(line, file=sys.stderr)
    return 1


def git(*args: str) -> str:
    try:
        out = subprocess.run(
            ["git", "-C", str(ROOT), *args],
            capture_output=True, text=True, check=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return ""
    return out.stdout.strip()


def newest_tag() -> str:
    tags = git("tag", "--list", "v[0-9]*", "--sort=-v:refname")
    return tags.splitlines()[0] if tags else ""


def citation_orcid() -> str:
    try:
        import yaml
    except ImportError:
        return ""
    try:
        cff = yaml.safe_load((ROOT / "CITATION.cff").read_text(encoding="utf-8"))
    except (OSError, yaml.YAMLError):
        return ""
    for author in cff.get("authors", []) if isinstance(cff, dict) else []:
        orcid = (author or {}).get("orcid", "")
        if orcid:
            return orcid.rstrip("/").rsplit("/", 1)[-1]
    return ""


def fetch_works(orcid: str, timeout: float = 30.0) -> list[dict]:
    req = urllib.request.Request(
        f"{PUBLIC_API}/{orcid}/works",
        headers={"Accept": "application/xml", "User-Agent": USER_AGENT},
    )
    with urllib.request.urlopen(req, timeout=timeout) as response:
        root = ET.fromstring(response.read())
    works = []
    for summary in root.findall(".//work:work-summary", NS):
        title = summary.find(".//work:title/common:title", NS)
        self_doi = concept_doi = ""
        for ext in summary.findall(".//common:external-ids/common:external-id", NS):
            kind = ext.findtext("common:external-id-type", "", NS).lower()
            value = ext.findtext("common:external-id-value", "", NS)
            rel = ext.findtext("common:external-id-relationship", "", NS)
            if kind == "doi":
                if rel == "self":
                    self_doi = value
                elif rel == "version-of":
                    concept_doi = value
        works.append({
            "put_code": summary.get("put-code", ""),
            "display_index": int(summary.get("display-index") or "1"),
            "title": (title.text or "").strip() if title is not None else "",
            "self_doi": self_doi,
            "concept_doi": concept_doi,
        })
    return works


def title_for(kind: str, version: str) -> str:
    if kind == "source":
        return f"Bovnar {version}"
    return f"Bovnar {version} Documentation and Specification"


def find_target(works: list[dict], kind: str, version: str,
                doi: str | None) -> dict | None:
    if doi:
        wanted = doi.strip().lower()
        for work in works:
            if work["self_doi"].lower() == wanted:
                return work
    wanted = title_for(kind, version)
    for work in works:
        if work["title"] == wanted:
            return work
    for work in works:
        if work["title"].casefold() == wanted.casefold():
            return work
    return None


def group_of(works: list[dict], target: dict) -> list[dict]:
    concept = target["concept_doi"]
    if not concept:
        return [target]
    return [work for work in works if work["concept_doi"] == concept]


def preferred(group: list[dict]) -> list[dict]:
    top = max(work["display_index"] for work in group)
    return [work for work in group if work["display_index"] == top]


def describe(work: dict) -> str:
    return (f'"{work["title"]}" (put-code {work["put_code"]}, '
            f'display-index {work["display_index"]})')


def cookie_header(path: Path) -> str:
    try:
        raw = path.read_text(encoding="utf-8")
    except OSError:
        return ""
    lines = [
        line for line in raw.splitlines()
        if line.strip() and (not line.lstrip().startswith("#")
                             or line.lstrip().startswith("#HttpOnly_"))
    ]
    if not lines:
        return ""
    if any("\t" in line for line in lines):
        pairs = []
        for line in lines:
            fields = line.split("\t")
            if len(fields) < 7:
                continue
            domain = fields[0].removeprefix("#HttpOnly_")
            if "orcid.org" in domain:
                pairs.append(f"{fields[5]}={fields[6]}")
        return "; ".join(pairs)
    header = " ".join(line.strip() for line in lines)
    if header.lower().startswith("cookie:"):
        header = header[7:].strip()
    return header


def update_to_max_display(put_code: str, orcid: str, cookie: str) -> tuple[bool, str]:
    conn = http.client.HTTPSConnection(WEB_HOST, timeout=30)
    try:
        conn.request(
            "GET",
            f"/works/updateToMaxDisplay.json?putCode={put_code}",
            headers={
                "Cookie": cookie,
                "User-Agent": ("Mozilla/5.0 (X11; Linux x86_64) "
                               "AppleWebKit/537.36 (KHTML, like Gecko) "
                               "Chrome/131.0 Safari/537.36"),
                "Accept": "application/json, text/plain, */*",
                "Referer": f"https://{WEB_HOST}/my-orcid?orcid={orcid}",
                "X-Requested-With": "XMLHttpRequest",
            },
        )
        response = conn.getresponse()
        status = response.status
        location = response.getheader("Location") or ""
        body = response.read().decode("utf-8", "replace")
    finally:
        conn.close()
    if status in (301, 302, 303, 307, 308) or "/signin" in location:
        return False, "the ORCID session cookie is missing or expired (redirected to sign-in)"
    if status != 200:
        return False, f"ORCID answered HTTP {status}: {body[:200]}"
    if body.strip().lower() not in ("true", "1"):
        return False, f"ORCID answered {body[:200]!r}, not a success"
    return True, ""


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Make a Bovnar release's works the ORCID preferred source.")
    parser.add_argument("--tag", help="release tag (default: newest v* tag)")
    parser.add_argument("--version", help="release version (default: from the tag)")
    parser.add_argument("--orcid", help="ORCID iD (default: from CITATION.cff)")
    parser.add_argument("--source-doi", help="source work DOI (default: match by title)")
    parser.add_argument("--docs-doi", help="documentation work DOI (default: match by title)")
    parser.add_argument("--cookie-file", default=str(DEFAULT_COOKIE),
                        help=f"session cookie file (default: {DEFAULT_COOKIE})")
    parser.add_argument("--check", action="store_true",
                        help="report and exit 1 unless both works are preferred; no cookie needed")
    parser.add_argument("--dry-run", action="store_true",
                        help="report what would be changed; do not update")
    parser.add_argument("--wait", type=float, default=0.0, metavar="SECONDS",
                        help="poll for missing works (DataCite auto-update lag)")
    parser.add_argument("--interval", type=float, default=30.0, metavar="SECONDS",
                        help="poll interval for --wait (default: 30)")
    args = parser.parse_args()

    tag = args.tag or newest_tag()
    if not tag:
        return fail("orcid_prefer.py: no release tag; pass --tag vX.Y.Z")
    version = args.version or (tag[1:] if tag.startswith("v") else tag)
    orcid = args.orcid or citation_orcid()
    if not orcid:
        return fail("orcid_prefer.py: no ORCID iD in CITATION.cff; pass --orcid")
    dois = {"source": args.source_doi, "docs": args.docs_doi}

    deadline = time.monotonic() + max(args.wait, 0.0)
    works = fetch_works(orcid)
    targets: dict[str, dict] = {}
    for kind in KINDS:
        target = find_target(works, kind, version, dois[kind])
        while target is None and time.monotonic() < deadline:
            print(f"waiting for the {kind} work ({title_for(kind, version)}) "
                  f"to reach ORCID...")
            time.sleep(max(args.interval, 1.0))
            works = fetch_works(orcid)
            target = find_target(works, kind, version, dois[kind])
        if target is None:
            seen = [work["title"] for work in works if version in work["title"]]
            return fail(
                f"orcid_prefer.py: {kind} work for {version} is not on "
                f"{orcid} yet" + (f"; works with that version: {seen}" if seen else ""),
                "Run again later with --wait, or pass --source-doi/--docs-doi.")
        targets[kind] = target

    needed: list[tuple[str, dict]] = []
    for kind in KINDS:
        target = targets[kind]
        group = group_of(works, target)
        top = preferred(group)
        print(f"{kind}: target {describe(target)}")
        print(f"  preferred: {', '.join(describe(work) for work in top)}")
        if target not in top:
            needed.append((kind, target))

    if args.check:
        if needed:
            print("not preferred: " + ", ".join(kind for kind, _ in needed))
            return 1
        print("both works are preferred")
        return 0
    if not needed:
        print("both works are already preferred; nothing to do")
        return 0
    if args.dry_run:
        for kind, target in needed:
            print(f"would make {kind} {describe(target)} preferred")
        return 0

    cookie_path = Path(args.cookie_file).expanduser()
    cookie = cookie_header(cookie_path)
    if not cookie:
        return fail(
            f"orcid_prefer.py: no ORCID session cookie in {cookie_path}",
            "Sign in at https://orcid.org, then copy the Cookie request header",
            "(DevTools -> Network -> any orcid.org request -> Headers) into",
            f"{cookie_path}, one line; chmod 600 it. A Netscape cookies.txt",
            "export for orcid.org works too.")
    if "SESSION=" not in cookie and "JSESSIONID=" not in cookie:
        hint = ""
        if "Path=" in cookie or "Expires=" in cookie:
            hint = (" The file looks like a Set-Cookie response line (it has "
                    "Expires/Path attributes), not a request Cookie header.")
        return fail(
            f"orcid_prefer.py: {cookie_path} has no ORCID session cookie."
            f"{hint}",
            "DevTools -> Network -> a request to orcid.org -> Request Headers:",
            "copy the whole `cookie:` value, not Response Headers / Set-Cookie.",
            "It should contain SESSION=... (or JSESSIONID=...).")
    try:
        if cookie_path.stat().st_mode & 0o077:
            print(f"warning: {cookie_path} is readable by group/others; "
                  "chmod 600 it", file=sys.stderr)
    except OSError:
        pass

    for kind, target in needed:
        ok, error = update_to_max_display(target["put_code"], orcid, cookie)
        if not ok:
            return fail(f"orcid_prefer.py: {kind}: {error}",
                        "Refresh the cookie file from a signed-in orcid.org session.")
        print(f"set {kind} {target['title']} preferred")

    works = fetch_works(orcid)
    for kind in KINDS:
        target = find_target(works, kind, version, dois[kind])
        if target is None:
            return fail(f"orcid_prefer.py: {kind} work disappeared after the update")
        top = preferred(group_of(works, target))
        if target not in top:
            return fail(f"orcid_prefer.py: {kind} is still not preferred; "
                        "ORCID did not record the change")
        print(f"{kind}: verified preferred {describe(target)}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except urllib.error.URLError as error:
        sys.exit(f"orcid_prefer.py: network error: {error}")
    except KeyboardInterrupt:
        sys.exit(130)
