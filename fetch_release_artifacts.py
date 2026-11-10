#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""fetch_release_artifacts.py — copy a release's archives into the tree that
publish_web.sh uploads to /download/<version>/.

The landing page's Downloads menu serves the archives from this host, not from
GitHub: every link is /download/<version>/<asset>. The archives are build
outputs (17 MB and up) and do not belong in git, so this downloads them once
per tag from the GitHub release CI attached them to, checks them against the
release's own SHA256SUMS, and leaves them in build/releases/<version>/.
publish_web.sh uploads that directory to the web root; its --serve preview
publishes it too.

The names carry the version and the tagged commit's count, so the expected set
is recomputed from the tag the same way the release job named it -- never typed
by hand. A rerun keeps files whose hash already matches, so only a changed or
missing asset costs a download.

Usage:
    python3 fetch_release_artifacts.py             # newest v* tag
    python3 fetch_release_artifacts.py --tag v2.1.0
    python3 fetch_release_artifacts.py --check     # verify only, no network
"""
import argparse
import hashlib
import os
import re
import shutil
import sys
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ROOT)

from check_release_links import (REPO, expected_assets, git, newest_tag,  # noqa: E402
                                 version_at)

DEST_ROOT = os.path.join(ROOT, "build", "releases")
BASE = f"https://github.com/{REPO}/releases/download"
USER_AGENT = "bovnar-release-artifacts (fetch_release_artifacts.py)"


def fail(*lines):
    for line in lines:
        print(line, file=sys.stderr)
    return 1


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_sums(path):
    """The release's checksum file: `<sha256>  <name>` (sha256sum's spacing)."""
    sums = {}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            m = re.match(r"^([0-9a-fA-F]{64})\s+\*?(.+?)\s*$", line)
            if m:
                sums[m.group(2)] = m.group(1).lower()
    return sums


def download(url, path):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=180) as r, open(path, "wb") as fh:
        shutil.copyfileobj(r, fh)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tag", help="release tag (default: the newest v* tag)")
    ap.add_argument("--check", action="store_true",
                    help="verify an existing build/releases/<version> only; "
                         "download nothing")
    a = ap.parse_args()

    tag = a.tag or newest_tag()
    if not tag:
        return fail("fetch_release_artifacts.py: no v* tag in this clone.",
                    "Run:  git fetch --tags")
    version = version_at(tag)
    if not version:
        return fail(f"fetch_release_artifacts.py: {tag}: could not read "
                    "project(bovnar VERSION ...) from CMakeLists.txt at that tag.")
    if tag != f"v{version}":
        return fail(f"fetch_release_artifacts.py: {tag} records project version "
                    f"{version} in CMakeLists.txt, so the release job named its "
                    f"archives bovnar-{version}-…, not bovnar-{tag.lstrip('v')}-….")
    count = git("rev-list", "--count", tag)
    if not count or not count.isdigit():
        return fail(f"fetch_release_artifacts.py: {tag}: could not count commits "
                    "(git rev-list --count).")

    assets = expected_assets(version, count)
    dest = os.path.join(DEST_ROOT, version)
    os.makedirs(dest, exist_ok=True)

    sums_path = os.path.join(dest, "SHA256SUMS")
    if a.check:
        if not os.path.isdir(dest):
            return fail(f"fetch_release_artifacts.py: {dest} does not exist.")
    else:
        print(f"==> Fetching {tag} artifacts into {os.path.relpath(dest, ROOT)}")
        try:
            download(f"{BASE}/{tag}/SHA256SUMS", sums_path)
        except (urllib.error.HTTPError, urllib.error.URLError, OSError) as e:
            return fail(f"fetch_release_artifacts.py: {tag}/SHA256SUMS: {e}",
                        "The release job may not have attached the archives yet.")

    if not os.path.isfile(sums_path):
        return fail(f"fetch_release_artifacts.py: {sums_path} is missing; run "
                    "without --check first.")
    sums = read_sums(sums_path)
    if not sums:
        return fail(f"fetch_release_artifacts.py: {sums_path} lists no checksums.")

    missing, bad = [], []
    for name in assets:
        if name == "SHA256SUMS":
            continue
        path = os.path.join(dest, name)
        if name in sums and os.path.isfile(path) and sha256(path) == sums[name]:
            print(f"  {name}: current")
            continue
        if a.check:
            missing.append(name)
            continue
        try:
            print(f"  {name}: downloading")
            download(f"{BASE}/{tag}/{name}", path)
        except urllib.error.HTTPError as e:
            missing.append(f"{name} ({e.code})")
            continue
        except (urllib.error.URLError, OSError) as e:
            missing.append(f"{name} ({e})")
            continue
        got = sha256(path)
        if name not in sums:
            bad.append(f"{name}: not listed in SHA256SUMS")
        elif got != sums[name]:
            bad.append(f"{name}: sha256 {got} != {sums[name]}")

    if missing:
        return fail("fetch_release_artifacts.py: missing asset(s):",
                    *[f"  {m}" for m in missing],
                    "The release job may still be running; rerun when it finishes.")
    if bad:
        return fail("fetch_release_artifacts.py: checksum mismatch:",
                    *[f"  {b}" for b in bad])
    listed = set(sums) - {"SHA256SUMS"}
    unexpected = sorted(listed - set(assets))
    if unexpected:
        return fail("fetch_release_artifacts.py: SHA256SUMS names assets the "
                    "release job was not expected to attach:",
                    *[f"  {u}" for u in unexpected])
    print(f"fetch_release_artifacts.py: {len(assets) - 1} archive(s) verified "
          f"against SHA256SUMS in {os.path.relpath(dest, ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
