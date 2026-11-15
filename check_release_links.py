#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""
check_release_links.py — the site's Downloads menu points at this line's release.

The landing page's nav carries a Downloads menu whose links are one release's
archives, served from this host at /download/<version>/. The archive names
cannot be generic: CI packs them as `bovnar-<version>-<count>-…`, where
<count> is the tagged commit's total commit count, so the menu is pinned to a
release by hand — and a hand-written URL set that has to be retyped at every
release is precisely the thing that gets forgotten. Forgotten here it is
silent: the links keep resolving, keep downloading, and keep handing out the
PREVIOUS version to everyone who takes the site at its word.

Which release that is depends on the checkout, so a patch of an older minor
stays green after a higher minor exists:

  * on main, and on any branch that is not a maintenance line, the highest
    library version whose tag implements the highest specification;
  * on an X.Y.x branch, and on a vX.Y.Z tag, the newest tag of that minor;
  * a line that has no tag yet keeps the main rule, because its menu still
    names the release it was cut from.

So this recomputes what the release job would have produced and compares:

  * that release is the one the menu links to;
  * every asset filename matches what pack_artifacts.cmake names for that tag's
    version and commit count -- derived the way the packer derives them, from
    project() in CMakeLists.txt AT THE TAG and `git rev-list --count <tag>`,
    not from the tag string, so a tag cut without bumping project() is caught
    too (that mismatch would name every archive after the old version);
  * the version the panel prints in its heading is that same tag;
  * the menu's "All releases" link is the local /download/ page, and that page
    links the same release's files.

What it deliberately does not check: the human-readable sizes beside each link.
They are not derivable without asking github.com, and a test here must not
depend on the network (the same rule check_web_links.py works under). A stale
size mislabels a download; a stale URL ships the wrong software.

Note the ordering this implies when cutting a release: tag first, let CI build
and attach the archives, then update the menu. Updating the page first fails
here, and rightly -- until the tag exists those files do not.

Usage:
    python3 check_release_links.py            # exit non-zero on drift
    python3 check_release_links.py --verbose  # also print what matched
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(ROOT, "web", "index.html")
TAG_RE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")
BRANCH_RE = re.compile(r"^(\d+)\.(\d+)\.x$")

REPO = "sothis/bovnar"
SITE = "https://www.bovnar.io"
DOWNLOAD_PREFIX = "/download/"      # <version>/<asset>, served from this host
DOWNLOAD_PAGE = DOWNLOAD_PREFIX     # the all-releases page the menu links
GITHUB_DOWNLOAD_PREFIX = f"https://github.com/{REPO}/releases/download/"

# The nav's downloads menu, from <li class="nav-dl"> to its closing </li>. Only
# this block is examined: the page links to github.com in a dozen other places
# and none of them is a release asset.
BLOCK_RE = re.compile(r'<li class="nav-dl">(.*?)\n\s*</li>\s*\n\s*<li>', re.S)
HREF_RE = re.compile(r'href="([^"]+)"')
# "Latest release <b>v1.1.0</b>" — the version the panel shows the reader.
HEAD_RE = re.compile(r'<div class="nav-dl-head">[^<]*<b>([^<]+)</b>')
# The schema.org SoftwareApplication entry's own statement of the version.
SOFTWARE_VERSION_RE = re.compile(r'"softwareVersion":\s*"([^"]+)"')


def git(*args):
    """Run git in the repo; return stdout, or None if git or the command fails."""
    try:
        out = subprocess.run(("git", "-C", ROOT) + args, capture_output=True,
                             text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    return out.stdout.strip()


def listed_tags():
    """Every v<major>.<minor>.<patch> tag, highest version first.

    --sort=-v:refname orders by version number rather than lexically, so v1.10.0
    sorts above v1.9.0 the way a release does and not the way a string does.
    """
    out = git("tag", "--list", "v[0-9]*", "--sort=-v:refname")
    if not out:
        return []
    return [line.strip() for line in out.splitlines() if TAG_RE.match(line.strip())]


def newest_tag():
    """The highest v<major>.<minor>.<patch> tag, or None if there are none."""
    names = listed_tags()
    return names[0] if names else None


def version_tuple(tag):
    m = TAG_RE.match(tag)
    if not m:
        return None
    return tuple(int(p) for p in m.groups())


def spec_of(tag):
    """(major, minor) of BVNR_SPEC_VERSION at `tag`, or None when it has none."""
    text = git("show", f"{tag}:include/bovnar.h")
    if not text:
        return None
    found = []
    for name in ("MAJOR", "MINOR"):
        m = re.search(rf"^[ \t]*#define[ \t]+BVNR_SPEC_VERSION_{name}[ \t]+(\d+)\b",
                      text, re.M)
        if not m:
            return None
        found.append(int(m.group(1)))
    return tuple(found)


def checkout_ref():
    """(kind, name) of the ref this run is about.

    kind is 'tag', 'branch', or 'other'. CI's GITHUB_REF wins, because a tag
    checkout is detached and its abbrev-ref is HEAD.
    """
    ref = os.environ.get("GITHUB_REF", "")
    if ref.startswith("refs/tags/"):
        return "tag", ref[len("refs/tags/"):]
    if ref.startswith("refs/heads/"):
        return "branch", ref[len("refs/heads/"):]
    name = git("rev-parse", "--abbrev-ref", "HEAD")
    if name and name not in ("", "HEAD"):
        return "branch", name
    tag = git("describe", "--exact-match", "--tags", "HEAD")
    if tag:
        return "tag", tag
    return "other", ""


def choose_menu_tag(entries, kind, name):
    """The tag this checkout's Downloads menu must name. Pure.

    `entries` is (tag, spec) with spec a (major, minor) pair or None. On a
    maintenance branch or a version tag, that minor's newest tag. A minor
    with no tag yet, and every other checkout, names the highest library
    version that implements the highest specification.
    """
    parsed = [(t, s) for t, s in entries if version_tuple(t)]

    def line_newest(major, minor):
        line = [t for t, _s in parsed if version_tuple(t)[:2] == (major, minor)]
        return max(line, key=version_tuple) if line else None

    if kind == "tag":
        m = TAG_RE.match(name or "")
        if m:
            chosen = line_newest(int(m.group(1)), int(m.group(2)))
            if chosen:
                return chosen
    if kind == "branch":
        m = BRANCH_RE.match(name or "")
        if m:
            chosen = line_newest(int(m.group(1)), int(m.group(2)))
            if chosen:
                return chosen
    with_spec = [(t, s) for t, s in parsed if s is not None]
    if not with_spec:
        return max((t for t, _s in parsed), key=version_tuple) if parsed else None
    best = max(s for _t, s in with_spec)
    pool = [t for t, s in with_spec if s == best]
    return max(pool, key=version_tuple)


def menu_tag():
    """The release the Downloads menu of this checkout must name."""
    names = listed_tags()
    if not names:
        return None
    kind, name = checkout_ref()
    return choose_menu_tag([(t, spec_of(t)) for t in names], kind, name)


def menu_reason(tag, kind, name):
    """Why `tag` is the release this checkout's menu must name."""
    line = None
    if kind == "branch" and BRANCH_RE.match(name or ""):
        m = BRANCH_RE.match(name)
        line = (int(m.group(1)), int(m.group(2)))
    elif kind == "tag" and version_tuple(name or ""):
        line = version_tuple(name)[:2]
    if line and tag and version_tuple(tag)[:2] == line:
        return f"{tag}, the newest tag of {line[0]}.{line[1]}"
    spec = spec_of(tag) if tag else None
    if spec:
        return f"{tag}, the highest release of spec {spec[0]}.{spec[1]}"
    return tag or ""


def _self_test():
    """The menu's choice of release, with no git and no page."""
    entries = [
        ("v3.0.0", (1, 2)),
        ("v2.3.1", (1, 3)),
        ("v2.3.0", (1, 3)),
        ("v2.2.9", (1, 2)),
        ("v2.2.0", (1, 2)),
    ]
    cases = (
        ("other", "", "v2.3.1"),
        ("branch", "main", "v2.3.1"),
        ("branch", "2.2.x", "v2.2.9"),
        ("tag", "v2.2.9", "v2.2.9"),
        ("branch", "2.3.x", "v2.3.1"),
        ("branch", "3.0.x", "v3.0.0"),
        ("tag", "v3.0.0", "v3.0.0"),
        ("branch", "2.4.x", "v2.3.1"),
    )
    for kind, name, want in cases:
        got = choose_menu_tag(entries, kind, name)
        if got != want:
            return fail(f"check_release_links.py: {kind} {name or '(none)'} "
                        f"chose {got}, wanted {want}")
    # The same spec on two majors: the higher library version is the menu.
    got = choose_menu_tag([("v3.0.0", (1, 3)), ("v2.4.0", (1, 3))], "branch", "main")
    if got != "v3.0.0":
        return fail(f"check_release_links.py: the higher library version of a "
                    f"shared spec chose {got}")
    # Version order is numeric: v1.10.0 is newer than v1.9.0.
    got = choose_menu_tag([("v1.9.0", (1, 0)), ("v1.10.0", (1, 0))], "branch", "main")
    if got != "v1.10.0":
        return fail(f"check_release_links.py: v1.10.0 sorted as {got}")
    return 0


def version_at(tag):
    """The project() version recorded in CMakeLists.txt at `tag`.

    This is the single source of truth pack_artifacts.cmake and the source-tarball
    job both read; taking it from the tag NAME instead would agree with them right
    up until the release where it does not.
    """
    src = git("show", f"{tag}:CMakeLists.txt")
    if src is None:
        return None
    m = re.search(r"project\(bovnar VERSION (\d+\.\d+\.\d+)", src)
    return m.group(1) if m else None


def has_freebsd_asset(version):
    """Whether this release carries a FreeBSD archive.

    The FreeBSD port and its archive landed in 2.1.0. Earlier tags have no such
    asset, so their menus must not be expected to link one -- the expectation
    moves with the version rather than with the calendar.
    """
    return tuple(int(p) for p in version.split(".")) >= (2, 1, 0)


def expected_assets(version, count):
    """Filenames the release job attaches, in the order the menu lists them.

    Mirrors cmake/pack_artifacts.cmake (the amalgamation and one archive per
    platform and toolchain, FreeBSD from 2.1.0), the source-tarball job, and
    the SHA256SUMS the release job writes beside them.
    """
    tag = f"{version}-{count}"
    assets = [f"bovnar-linux-{tag}.tar.xz"]
    if has_freebsd_asset(version):
        assets.append(f"bovnar-freebsd-{tag}.tar.xz")
    assets += [
        f"bovnar-macos-{tag}.tar.xz",
        f"bovnar-windows-msvc-{tag}.zip",
        f"bovnar-windows-mingw-{tag}.zip",
        f"bovnar-{tag}-amalgamate.tar.xz",
        f"bovnar-{tag}-source.tar.xz",
        "SHA256SUMS",
    ]
    return assets


def fail(*lines):
    for line in lines:
        print(line, file=sys.stderr)
    return 1


def main():
    verbose = "--verbose" in sys.argv[1:]
    if _self_test():
        return 1

    with open(PAGE, encoding="utf-8") as fh:
        html = fh.read()

    block = BLOCK_RE.search(html)
    if not block:
        return fail(
            "web/index.html: no <li class=\"nav-dl\"> downloads menu found.",
            "If the menu was removed on purpose, remove this check with it;",
            "otherwise its markup has changed shape and this gate is now blind.")
    block = block.group(1)

    hrefs = HREF_RE.findall(block)
    assets = [h[len(DOWNLOAD_PREFIX):] for h in hrefs
              if h.startswith(DOWNLOAD_PREFIX) and h != DOWNLOAD_PAGE]
    github_assets = [h for h in hrefs if h.startswith(GITHUB_DOWNLOAD_PREFIX)]

    if github_assets:
        return fail(
            "web/index.html: the downloads menu still sends archives to GitHub:",
            *[f"  {h}" for h in github_assets],
            f"The archives are served from this host; link them as "
            f"{DOWNLOAD_PREFIX}<version>/<asset>.")

    if not assets:
        return fail("web/index.html: the downloads menu links to no release "
                    "asset at all.")
    if hrefs.count(DOWNLOAD_PAGE) != 1:
        return fail(f"web/index.html: the downloads menu must carry exactly one "
                    f"{DOWNLOAD_PAGE} link (the local all-releases page).")

    # --- the page agrees with itself ------------------------------------------
    # The JSON-LD block further down states a softwareVersion of its own. It is
    # outside the menu and nothing else reads it, so the menu could move to a
    # new release and leave the structured data -- what a search engine quotes
    # -- on the old one. This needs no git, so it holds in a tarball too.
    panel = HEAD_RE.search(block)
    declared = SOFTWARE_VERSION_RE.findall(html)
    if panel and declared != [panel.group(1).lstrip("v")]:
        return fail(
            f"web/index.html: the downloads menu is headed {panel.group(1)} "
            f"but the JSON-LD \"softwareVersion\" says "
            f"{', '.join(declared) if declared else 'nothing'}.",
            "They name the same release; move them together.")

    # --- what this checkout's menu must name ----------------------------------
    # Ask git rather than looking for a .git DIRECTORY: in a `git worktree` it is
    # a file, and the isdir() spelling would have skipped this check entirely
    # from any worktree -- exactly the silent self-disabling it exists to prevent.
    # And ask for the toplevel, not merely "am I inside a work tree": a source
    # tarball unpacked somewhere under an UNRELATED repository is inside that
    # repository's work tree, and was then judged against its tags.
    top = git("rev-parse", "--show-toplevel")
    if top is None or os.path.realpath(top) != os.path.realpath(ROOT):
        # The source tarball is `git archive` output: tracked files, no history.
        # Nothing to compare against, and nothing wrong either.
        print("check_release_links.py: not a git checkout (source tarball?) -- "
              "the menu's release tag cannot be verified here")
        return 0
    if git("rev-parse", "--is-shallow-repository") == "true":
        return fail(
            "this is a shallow clone: `git rev-list --count` would report a "
            "commit count the release job never used, so the asset names cannot "
            "be checked.",
            "Run:  git fetch --unshallow --tags")

    kind, ref_name = checkout_ref()
    tag = menu_tag()
    if not tag:
        return fail(
            "no v* tag in this clone, so there is no release to check the "
            "downloads menu against.",
            "Run:  git fetch --tags")
    reason = menu_reason(tag, kind, ref_name)

    version = version_at(tag)
    if not version:
        return fail(f"{tag}: could not read project(bovnar VERSION ...) from "
                    "CMakeLists.txt at that tag.")

    # THE TAGGED COMMIT ITSELF IS EXEMPT. The order this gate imposes is "tag
    # first, let CI build and attach the archives, then update the menu" -- so
    # on the commit the tag points at, the menu still names the previous
    # release, and it has to: the files it would link to are what this very CI
    # run is about to produce. Failing here made the tag's own test job red,
    # which withheld the archives from the release it was guarding. The check
    # is back in force on the first commit after the tag.
    head = git("rev-parse", "HEAD")
    if head and head == git("rev-parse", f"{tag}^{{commit}}"):
        if tag != f"v{version}":
            return fail(
                f"{tag} records project(bovnar VERSION {version}) in "
                "CMakeLists.txt; the tag and the project version disagree.")
        print(f"check_release_links.py: HEAD is {tag} itself -- its archives "
              "do not exist yet, so the menu is not checked on this commit. "
              "Update web/index.html once the release job has attached them. "
              f"This checkout's menu then names {reason}.")
        return 0
    if tag != f"v{version}":
        return fail(
            f"{tag} records project(bovnar VERSION {version}) in CMakeLists.txt, "
            f"so the release job named its archives bovnar-{version}-… while the "
            f"tag itself says {tag}.",
            "One of the two was not bumped; fix that before trusting either.")

    count = git("rev-list", "--count", tag)
    if not count or not count.isdigit():
        return fail(f"{tag}: could not count commits (git rev-list --count).")

    # --- compare -------------------------------------------------------------
    want = expected_assets(version, count)
    want_prefixed = [f"{version}/{a}" for a in want]

    if assets != want_prefixed:
        wrong_version = sorted({a.split("/", 1)[0] for a in assets} - {version})
        lines = [f"web/index.html: the downloads menu does not match {reason} "
                 f"(version {version}, build count {count})."]
        if wrong_version:
            lines.append(f"  it links to version(s): {', '.join(wrong_version)}")
        lines.append("  expected, in order:")
        lines += [f"    {SITE}{DOWNLOAD_PREFIX}{a}" for a in want_prefixed]
        lines.append("  found:")
        lines += [f"    {SITE}{DOWNLOAD_PREFIX}{a}" for a in assets]
        lines.append("Repoint the hrefs (and the sizes beside them, which this "
                     "check cannot see), then re-run ./gen_i18n.py --extract de "
                     "and translate the new keys.")
        return fail(*lines)

    # The menu's "All releases" link points at this generated page; it has to
    # list the same release, or it sends readers to files that do not exist.
    page_path = os.path.join(ROOT, "web", "download", "index.html")
    if not os.path.isfile(page_path):
        return fail("web/download/index.html: the all-releases downloads page "
                    "is missing; run python3 gen_downloads.py.")
    page_html = open(page_path, encoding="utf-8").read()
    missing = [a for a in want_prefixed
               if f"{DOWNLOAD_PREFIX}{a}" not in page_html]
    if missing:
        return fail("web/download/index.html: it does not link the current "
                    "release's files:",
                    *[f"  {SITE}{DOWNLOAD_PREFIX}{a}" for a in missing],
                    "Run python3 gen_downloads.py (and fetch the archives first "
                    "if they are absent).")

    head = HEAD_RE.search(block)
    if not head:
        return fail('web/index.html: the downloads panel has no '
                    '<div class="nav-dl-head">…<b>version</b> heading.')
    if head.group(1) != tag:
        return fail(f"web/index.html: the downloads panel is headed "
                    f"\"{head.group(1)}\" but links to {reason}.")

    print(f"check_release_links.py: the downloads menu matches {reason} "
          f"({len(want)} assets, build count {count})")
    if verbose:
        for a in want_prefixed:
            print(f"  {SITE}{DOWNLOAD_PREFIX}{a}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
