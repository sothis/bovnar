#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""
check_release_branch.py — the tag was cut on its minor's branch, and the branch
holds nothing main does not.

CONTRIBUTING's Releases section says two things nothing else can see. A release
is cut on a branch named after the minor it serves -- 2.2.x for the 2.2 line --
and that branch exists before the tag does. And a patch release is a cherry-pick:
a fix lands on main first, and the branch takes it from there, so the branch
never holds work that exists nowhere else.

Neither is visible in the tree. A tag cut from main and a branch created
afterwards produce the same archive, the same wheel and the same green run; the
only thing that differs is where the next patch comes from. So this reads git
instead of files, and checks:

  * on a version tag: that vX.Y.Z is contained in X.Y.x, so the release was cut
    on the branch that will carry its patches;
  * on a maintenance branch: that every commit the branch has and main does not
    is a release commit or carries `git cherry-pick -x`'s "cherry picked from
    commit" trailer -- a fix written on the branch is a fix main will never get;
  * for every such commit: that it leaves the public surface alone. No change
    under tests/abi/ (the committed baseline check_abi.py compares against --
    `--update` in a patch commit is how an ABI break would be smuggled past the
    gate), and no hand-written header under include/. The version macros in
    include/bovnar.h may move, because that is what a release commit does, and
    the generated tables (include/*.gen.h) may move with the src/gendata/*.bvnr
    they come from, because a corrected conversion factor is a behaviour fix.

What it does not check: whether a change is a security or a behaviour fix rather
than a feature. That is a judgement, and the mechanical proxy here is the file
surface above, which is why a fix that must touch a prototype has to wait for a
minor.

Needs the branch and main in this clone, which the CI checkout has (fetch-depth
0); a source tarball has neither and is skipped rather than failed.

Usage:
    python3 check_release_branch.py                # from the ref CI pushed
    python3 check_release_branch.py --verbose      # also print what matched
    python3 check_release_branch.py --tag v2.2.0
    python3 check_release_branch.py --branch 2.2.x
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))

TAG_RE = re.compile(r"v(\d+)\.(\d+)\.(\d+)$")
BRANCH_RE = re.compile(r"(\d+)\.(\d+)\.x$")
# The only lines a release commit may change in the public header: this
# implementation's version. BVNR_SPEC_VERSION_* sits beside them and moves with
# the format, not with a patch.
VERSION_LINE_RE = re.compile(
    r"[-+]\s*#define\s+BVNR_VERSION_(MAJOR|MINOR|PATCH|STRING|SUFFIX|STRING_FULL)\b")
CHERRY_PICKED_RE = re.compile(r"cherry picked from commit [0-9a-f]{7,40}")


def git(*args):
    """Run git in the repo; return stdout, or None if git or the command fails."""
    try:
        out = subprocess.run(("git", "-C", ROOT) + args, capture_output=True,
                             text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    return out.stdout


def git_ok(*args):
    """Whether a git command succeeds -- for the ones that answer in the status."""
    try:
        return subprocess.run(("git", "-C", ROOT) + args, capture_output=True,
                              text=True).returncode == 0
    except OSError:
        return False


def resolve(ref):
    """The commit `ref` names, or None. --verify --quiet is silent when unknown."""
    out = git("rev-parse", "--verify", "--quiet", ref + "^{commit}")
    return out.strip() if out else None


def resolve_any(*refs):
    """The first of `refs` that resolves: the remote-tracking ref before the
    local one, so the gate answers about the branch the server has."""
    for ref in refs:
        commit = resolve(ref)
        if commit:
            return ref, commit
    return None, None


def fail(*lines):
    for line in lines:
        print(line, file=sys.stderr)
    return 1


def detect(argv):
    """What ref this run is about: (kind, name), or (None, None) to stand down."""
    for i, arg in enumerate(argv):
        if arg == "--tag" and i + 1 < len(argv):
            return "tag", argv[i + 1]
        if arg == "--branch" and i + 1 < len(argv):
            return "branch", argv[i + 1]
    ref = os.environ.get("GITHUB_REF", "")
    if ref.startswith("refs/tags/"):
        return "tag", ref[len("refs/tags/"):]
    if ref.startswith("refs/heads/"):
        return "branch", ref[len("refs/heads/"):]
    # A pull request's GITHUB_REF is refs/pull/<n>/merge, which is not a branch
    # this policy is about: the push to the branch is where the gate belongs.
    name = git("rev-parse", "--abbrev-ref", "HEAD")
    if name and name.strip() not in ("", "HEAD"):
        return "branch", name.strip()
    tag = git("describe", "--exact-match", "--tags", "HEAD")
    return ("tag", tag.strip()) if tag else (None, None)


def non_version_lines(commit, path):
    """The changed lines of `path` in `commit` that are not a version macro."""
    out = git("show", "--format=", "--unified=0", commit, "--", path) or ""
    return [line for line in out.splitlines()
            if line[:1] in "+-" and not line.startswith(("+++", "---"))
            and not VERSION_LINE_RE.match(line)]


def check_tag(tag, verbose):
    """vX.Y.Z must be contained in X.Y.x: the release was cut on its branch."""
    m = TAG_RE.search(tag)
    if not m:
        print(f"check_release_branch.py: {tag} is not a vX.Y.Z tag -- "
              f"nothing to check")
        return 0
    branch = f"{m.group(1)}.{m.group(2)}.x"
    _, head = resolve_any(f"refs/remotes/origin/{tag}", f"refs/tags/{tag}")
    if head is None:
        return fail(f"{tag}: the tag is not in this clone, so the branch it was "
                    f"cut on cannot be checked")
    name, tip = resolve_any(f"refs/remotes/origin/{branch}", f"refs/heads/{branch}")
    if tip is None:
        # Absent locally is only proof of absence when origin's refs are here.
        if resolve("refs/remotes/origin/main") is None:
            print(f"check_release_branch.py: origin's branches are not in this "
                  f"clone (source tarball?) -- {tag} cannot be checked against "
                  f"{branch}")
            return 0
        return fail(
            f"{tag}: there is no {branch} branch.",
            "CONTRIBUTING's Releases: the maintenance branch exists before the "
            "release commit, and the tag names that commit.")
    if verbose:
        print(f"check_release_branch.py: {tag} is {head[:12]}, {name} is "
              f"{tip[:12]}")
    if not git_ok("merge-base", "--is-ancestor", head, tip):
        return fail(
            f"{tag} is not contained in {branch}: the release was cut somewhere "
            f"the branch does not reach.",
            f"  tag    {head[:12]}",
            f"  branch {tip[:12]}",
            "Cut the tag from the branch, not from main.")
    print(f"check_release_branch.py: {tag} is contained in {branch}")
    return 0


def check_branch(branch, verbose):
    """The branch carries only release commits and cherry-picks, and it leaves
    the public surface alone."""
    if not BRANCH_RE.search(branch):
        print(f"check_release_branch.py: {branch} is not a X.Y.x maintenance "
              f"branch -- nothing to check")
        return 0
    base_name, base = resolve_any("refs/remotes/origin/main", "refs/heads/main")
    name, tip = resolve_any(f"refs/remotes/origin/{branch}", f"refs/heads/{branch}")
    if tip is None:
        print(f"check_release_branch.py: {branch} is not in this clone -- "
              f"nothing to check")
        return 0
    if base is None:
        return fail(f"{branch}: main is not in this clone, so there is nothing "
                    f"to say what the branch holds that main does not")
    if base == tip:
        print(f"check_release_branch.py: {branch} is at {base_name} ({base[:12]})")
        return 0

    # `git cherry` compares by patch-id, so a cherry-pick whose conflict was
    # resolved differently shows as `+` even though main has it -- hence the
    # trailer below, which `cherry-pick -x` writes and a rewrite does not.
    out = git("cherry", "-v", base, tip) or ""
    bad = []
    # flush: stderr is unbuffered and this line is what makes a failure list
    # readable (which branch, against which base).
    print(f"check_release_branch.py: {name} ({tip[:12]}) against "
          f"{base_name} ({base[:12]})", flush=True)
    for line in out.splitlines():
        if not line.startswith("+ "):
            continue
        sha, _, subject = line[2:].partition(" ")
        body = git("show", "-s", "--format=%B", sha) or ""
        if subject.startswith("release:"):
            kind = "release commit"
        elif CHERRY_PICKED_RE.search(body):
            kind = "cherry-pick"
        else:
            bad.append((sha, subject, "written on the branch, not on main: main "
                                      "will never get it"))
            continue
        if verbose:
            print(f"check_release_branch.py: {sha[:12]} {kind}: {subject}")
        bad += [(sha, subject, why) for why in surface(sha, kind)]

    if bad:
        lines = [f"{branch}: {len(bad)} problem(s) on the branch:"]
        lines += [f"  {sha[:12]} {subject}: {why}" for sha, subject, why in bad]
        lines.append("A patch release is a cherry-pick of a fix that is already "
                     "on main, and it moves no public surface. A commit listed "
                     "here that main does have is one this clone has not pushed.")
        return fail(*lines)
    print(f"check_release_branch.py: {branch} carries only what main has, and "
          f"no surface change")
    return 0


def surface(commit, kind):
    """Why `commit` may not be on a patch branch, as a list of reasons."""
    files = (git("show", "--pretty=format:", "--name-only", commit) or "").split()
    reasons = []
    for path in files:
        if path.startswith("tests/abi/"):
            reasons.append(f"touches {path}, the ABI baseline -- a patch branch "
                           f"does not move it")
        elif path.startswith("include/") and not path.endswith(".gen.h"):
            if path == "include/bovnar.h" and kind == "release commit":
                moved = non_version_lines(commit, path)
                if moved:
                    reasons.append(f"changes include/bovnar.h beyond the version "
                                   f"macros: {moved[0].strip()[:70]}")
            else:
                reasons.append(f"changes the hand-written header {path}")
    return reasons


def main():
    argv = sys.argv[1:]
    verbose = "--verbose" in argv
    kind, name = detect(argv)
    if kind == "tag":
        return check_tag(name, verbose)
    if kind == "branch":
        return check_branch(name, verbose)
    print("check_release_branch.py: not on a version tag or a maintenance "
          "branch -- nothing to check")
    return 0


if __name__ == "__main__":
    sys.exit(main())
