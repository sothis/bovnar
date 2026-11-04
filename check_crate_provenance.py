#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""
check_crate_provenance.py — the crate is the commit its PROVENANCE.json names.

THE GAP THIS CLOSES. The published crate carries the whole C library under c/,
and a consumer that vendors it from there (to compile it, and to generate its
license notices) has to be able to say which upstream revision it shipped. Up to
2.0.1 the crate said nothing about that: it is staged outside git, so cargo
records no VCS info, and no file named a commit. That the files of 2.0.1 are
the tag's could be shown only by staging the tag again and comparing.

rust/package.py now writes PROVENANCE.json into the crate (repository, commit,
tag, and every file's SHA-256 and origin). This script is the gate that makes
the manifest worth trusting:

  * every file in the crate is listed, with the hash it has, and nothing listed
    is missing (in a .crate, cargo's own normalized Cargo.toml is not ours:
    the manifest's Cargo.toml is cargo's Cargo.toml.orig);
  * staged again from the named commit, the crate comes out the same, so the
    manifest, and with it every hash, is what that commit produces: the copied
    sources byte for byte, and the generated tables as its generators make them;
  * a named tag points at that commit, and with --tag it is the one expected;
  * a manifest from a work tree with uncommitted changes fails without
    --allow-dirty. package.py allows that for local runs, never for a publish.

Usage:
    python3 check_crate_provenance.py CRATE [--tag vX.Y.Z] [--allow-dirty]

CRATE is a staged crate directory or a .crate file. Needs this repository with
the named commit in it (a shallow clone of exactly that commit is enough).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ROOT, "rust"))

import package  # noqa: E402  rust/package.py: staging and the manifest

# Files cargo writes into a .crate that no source has.
CARGO_MADE = {"Cargo.toml", ".cargo_vcs_info.json"}


def unpack(crate_file, into):
    with tarfile.open(crate_file, "r:gz") as tar:
        tar.extractall(into, filter="data")
    tops = os.listdir(into)
    if len(tops) != 1:
        raise ValueError("%s does not unpack to one directory" % crate_file)
    return os.path.join(into, tops[0])


def present_files(crate):
    """{manifest path: path on disk} for the files a crate directory holds."""
    files = {rel: os.path.join(crate, rel) for rel in package.crate_files(crate)}
    if "Cargo.toml.orig" in files:            # a .crate, as cargo packed it
        for rel in CARGO_MADE:
            files.pop(rel, None)
        files["Cargo.toml"] = files.pop("Cargo.toml.orig")
    return files


def head_and_clean(root):
    head = package.git(root, "rev-parse", "HEAD").strip()
    clean = not package.git(root, "status", "--porcelain", "--untracked-files=no").strip()
    return head, clean


def restaged(commit, dirty, scratch):
    """The manifest staging `commit` again produces (from the work tree if dirty)."""
    head, clean = head_and_clean(ROOT)
    worktree = None
    if dirty:
        if head != commit:
            return None, "the manifest is of uncommitted changes on %s, but HEAD is %s" % (
                commit[:12], head[:12])
        root = ROOT
    elif head == commit and clean:
        root = ROOT
    else:
        if subprocess.run(["git", "-C", ROOT, "cat-file", "-e", commit + "^{commit}"],
                          capture_output=True).returncode != 0:
            return None, "commit %s is not in this repository" % commit
        worktree = os.path.join(scratch, "src")
        package.git(ROOT, "worktree", "add", "--detach", worktree, commit)
        root = worktree
    try:
        crate = os.path.join(scratch, "restaged", "bovnar")
        origin = package.stage(crate, root)
        return package.provenance(crate, origin, root), None
    finally:
        if worktree:
            package.git(ROOT, "worktree", "remove", "--force", worktree)


def check(path, allow_dirty=False, tag=None):
    """Problems with the crate at `path` (directory or .crate); [] when it holds."""
    problems = []
    scratch = tempfile.mkdtemp(prefix="bovnar-provenance-")
    try:
        crate = unpack(path, os.path.join(scratch, "crate")) if os.path.isfile(path) else path
        manifest_path = os.path.join(crate, package.MANIFEST)
        if not os.path.exists(manifest_path):
            return ["no %s in %s" % (package.MANIFEST, path)]
        with open(manifest_path, encoding="utf-8") as f:
            manifest = json.load(f)
        if manifest.get("schema") != 1:
            return ["unknown manifest schema %r" % manifest.get("schema")]

        # The files are the ones the manifest lists, with its hashes.
        listed = manifest["files"]
        have = present_files(crate)
        for rel in sorted(set(have) - set(listed)):
            problems.append("%s is in the crate but not in the manifest" % rel)
        for rel in sorted(set(listed) - set(have)):
            problems.append("%s is in the manifest but not in the crate" % rel)
        for rel in sorted(set(listed) & set(have)):
            if package.sha256(have[rel]) != listed[rel]["sha256"]:
                problems.append("%s does not have the manifest's SHA-256" % rel)

        commit, named = manifest["commit"], manifest["tag"]
        if manifest["dirty"] and not allow_dirty:
            problems.append("the crate was staged from uncommitted changes on %s"
                            % commit[:12])
        if tag and named != tag:
            problems.append("the manifest names tag %s, not %s" % (named, tag))
        if named:
            r = subprocess.run(["git", "-C", ROOT, "rev-parse", "--verify", "-q",
                                named + "^{commit}"], capture_output=True, text=True)
            if r.returncode != 0:
                problems.append("tag %s is not in this repository" % named)
            elif r.stdout.strip() != commit:
                problems.append("tag %s is %s, not the manifest's commit %s"
                                % (named, r.stdout.strip()[:12], commit[:12]))

        # The commit, staged again, gives this very manifest.
        again, why = restaged(commit, manifest["dirty"], scratch)
        if again is None:
            problems.append(why)
        else:
            for key in sorted(set(manifest) | set(again)):
                if key == "files":
                    continue
                if manifest.get(key) != again.get(key):
                    problems.append("%s: the manifest says %r, the commit gives %r"
                                    % (key, manifest.get(key), again.get(key)))
            mine, theirs = listed, again["files"]
            for rel in sorted(set(mine) | set(theirs)):
                if mine.get(rel) != theirs.get(rel):
                    problems.append("%s: not what commit %s stages (%s)" % (
                        rel, commit[:12],
                        "absent there" if rel not in theirs else
                        "absent here" if rel not in mine else "content differs"))
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("crate", help="a staged crate directory or a .crate file")
    ap.add_argument("--tag", help="require the manifest to name this tag")
    ap.add_argument("--allow-dirty", action="store_true",
                    help="accept a crate staged from uncommitted changes")
    args = ap.parse_args()
    problems = check(args.crate, args.allow_dirty, args.tag)
    if problems:
        print("check_crate_provenance: %s fails:" % args.crate, file=sys.stderr)
        for p in problems:
            print("  " + p, file=sys.stderr)
        return 1
    print("check_crate_provenance: %s is the commit its manifest names, file for file"
          % args.crate)
    return 0


if __name__ == "__main__":
    sys.exit(main())
