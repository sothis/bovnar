#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""
package.py — stage the bovnar crate as crates.io gets it, then run cargo on it.

In the repository, rust/build.rs compiles ../src and runs the table generators
in the repository root. A package made from rust/ would contain neither, so
rust/Cargo.toml keeps `publish = false` and the crate is published from a
staged copy instead:

    <out>/bovnar/
        Cargo.toml        rust/Cargo.toml without `publish = false`
        Cargo.lock, build.rs, README.md, src/
        LICENSE, THIRD_PARTY_NOTICES.md
        PROVENANCE.json   the commit all of it comes from, and every file's SHA-256
        c/include/        include/*.h
        c/src/            the C sources build.rs compiles, and every src/ header
        c/generated/      the generators' output (*.gen.inc)
        c/prebuilt/       one static library per target, from the platform jobs

build.rs sees c/generated/ and builds from c/ without running anything, so the
published crate needs a C compiler but no Python. tests/ and fuzz/ stay out:
they read ../examples and ../src/gendata, which only the repository has, and
the repository's CI runs them.

c/prebuilt/<triple>/ is the static library the job that builds that platform
already produces (x86_64 MSVC, both macOS architectures), so a crate on that
target links it and needs no C compiler at all. `--prebuilt DIR` takes a
directory of the `bovnar-prebuilt-<triple>` Actions artifacts; build.rs's
PREBUILT list says which. A publish refuses to run without them.

PROVENANCE.json is what a consumer that vendors the C library from the crate
ties it to: the repository, the commit and (on a release) the tag, and the
SHA-256 of every file next to it, with the repository path each was copied
from. Before cargo runs, the staged copy is checked against the commit it
names (check_crate_provenance.py): staged again from that commit, it must come
out byte for byte the same. `publish` refuses a tree with uncommitted changes
and a commit without its vX.Y.Z tag.

The staging directory must not be inside a git work tree (cargo would apply
that repository's .gitignore to the package), so the default is a fresh
temporary directory.

Usage:
    python3 rust/package.py [--out DIR] [--prebuilt DIR] [--no-cargo] [-- CARGO-ARGS...]

CARGO-ARGS default to `package`, which builds the .crate and verifies it by
compiling it on its own. --no-cargo stages and checks only, and removes a
staging directory it created. Other examples:
    python3 rust/package.py -- publish --dry-run
    python3 rust/package.py --prebuilt prebuilt -- publish   # needs a crates.io token
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GENERATORS_RE = re.compile(r"const GENERATORS: \[[^\]]+\] = \[(.*?)\];", re.S)
SOURCES_RE = re.compile(r"const SOURCES: \[[^\]]+\] = \[(.*?)\];", re.S)
PREBUILT_RE = re.compile(r"const PREBUILT: \[[^\]]+\] = \[(.*?)\];", re.S)
PUBLISH_RE = re.compile(r"^# Not from here:.*?^publish = false\n", re.S | re.M)
VERSION_RE = re.compile(r'^version = "([^"]+)"', re.M)

MANIFEST = "PROVENANCE.json"
REPOSITORY = "https://github.com/sothis/bovnar"


def git(root, *args):
    return subprocess.run(["git", "-C", root] + list(args), check=True,
                          capture_output=True, text=True).stdout


def tracked(root, prefix):
    out = subprocess.run(["git", "ls-files", "-z", "--", prefix], cwd=root,
                         check=True, capture_output=True).stdout
    return [p for p in out.decode().split("\0") if p]


def build_rs_list(root, regex):
    with open(os.path.join(root, "rust", "build.rs"), encoding="utf-8") as f:
        m = regex.search(f.read())
    if not m:
        sys.exit("package.py: cannot find %s in rust/build.rs" % regex.pattern[:20])
    return re.findall(r'"([^"]+)"', m.group(1))


def prebuilt_list(root):
    """build.rs's PREBUILT, as [(target triple, archive name)]."""
    flat = build_rs_list(root, PREBUILT_RE)
    return list(zip(flat[::2], flat[1::2], strict=True))


def stage(crate, root=ROOT, prebuilt=None):
    """Stage the crate from the work tree at `root` into `crate`.

    `prebuilt` is a directory holding `bovnar-prebuilt-<triple>/<archive>` for
    every triple rust/build.rs lists: the CI artifacts that let the crate build
    without a C compiler on those targets. They are copied into `c/prebuilt/`
    and get no `from` in the manifest -- they are build output of the commit,
    not a file of it.

    Returns {crate path: where it came from}: the repository path a file was
    copied from, or "generated" for the generators' output.
    """
    origin = {}

    def copy(rel, dest_rel):
        dest = os.path.join(crate, dest_rel)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copy2(os.path.join(root, rel), dest)
        origin[dest_rel] = rel

    # The crate itself, minus tests/ and fuzz/.
    for rel in tracked(root, "rust"):
        sub = rel[len("rust/"):]
        if sub.startswith(("tests/", "fuzz/")):
            continue
        copy(rel, sub)
    for rel in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        copy(rel, rel)

    toml = os.path.join(crate, "Cargo.toml")
    with open(toml, encoding="utf-8") as f:
        text = f.read()
    text, n = PUBLISH_RE.subn("", text)
    if n != 1:
        sys.exit("package.py: rust/Cargo.toml no longer has the `publish = false` "
                 "block this script lifts; update PUBLISH_RE")
    with open(toml, "w", encoding="utf-8") as f:
        f.write(text)
    origin["Cargo.toml"] = "rust/Cargo.toml, without its `publish = false` block"

    # The generated tables, made the way build.rs makes them in the repository.
    gen = os.path.join(crate, "c", "generated")
    os.makedirs(gen)
    env = dict(os.environ, BVNR_GENERATED_DIR=gen, PYTHONDONTWRITEBYTECODE="1")
    # The generators also refresh the committed include/*.gen.h. If that
    # changes anything, the headers in git are stale and so would the crate be.
    # (Compared before and after, so that an edit in progress elsewhere under
    # include/ is not taken for one; PROVENANCE.json marks such a tree dirty.)
    headers = tracked(root, "include")
    before = {rel: sha256(os.path.join(root, rel)) for rel in headers}
    for script in build_rs_list(root, GENERATORS_RE):
        subprocess.run([sys.executable, os.path.join(root, script)],
                       cwd=root, env=env, check=True, stdout=subprocess.DEVNULL)
    for name in os.listdir(gen):
        origin["c/generated/" + name] = "generated"
    stale = [rel for rel in headers if sha256(os.path.join(root, rel)) != before[rel]]
    if stale:
        sys.exit("package.py: the generators changed committed headers:\n  "
                 + "\n  ".join(stale)
                 + "\ncommit them first, so the crate matches the repository")

    # The C sources build.rs compiles, and every header under include/ and src/.
    sources = set(build_rs_list(root, SOURCES_RE))
    for rel in tracked(root, "include") + tracked(root, "src"):
        if rel.startswith("src/gendata/"):
            continue
        if rel.endswith(".h") or rel in sources:
            copy(rel, "c/" + rel)
    missing = [s for s in sources if not os.path.exists(os.path.join(crate, "c", s))]
    if missing:
        sys.exit("package.py: build.rs names sources git does not track: %s" % missing)

    # The prebuilt archives, one per triple build.rs names, from the artifacts
    # the platform jobs upload. All of them or none: a crate that silently lost
    # one would start needing a C compiler on a target it promised not to.
    if prebuilt:
        for triple, archive in prebuilt_list(root):
            src = os.path.join(prebuilt, "bovnar-prebuilt-" + triple, archive)
            if not os.path.isfile(src):
                sys.exit(f"package.py: {src} is missing (build.rs PREBUILT names "
                         f"{archive} for {triple})")
            dest = f"c/prebuilt/{triple}/{archive}"
            os.makedirs(os.path.dirname(os.path.join(crate, dest)), exist_ok=True)
            shutil.copy2(src, os.path.join(crate, dest))
            origin[dest] = None
    return origin


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 16), b""):
            h.update(block)
    return h.hexdigest()


def crate_files(crate):
    """Every file under `crate` but the manifest, as sorted '/'-separated paths."""
    out = []
    for dirpath, _, names in os.walk(crate):
        for name in names:
            rel = os.path.relpath(os.path.join(dirpath, name), crate).replace(os.sep, "/")
            if rel != MANIFEST:
                out.append(rel)
    return sorted(out)


def provenance(crate, origin, root=ROOT):
    """The manifest for a crate staged from `root`, as a dict."""
    with open(os.path.join(crate, "Cargo.toml"), encoding="utf-8") as f:
        version = VERSION_RE.search(f.read())
    if version is None:
        sys.exit("package.py: the staged Cargo.toml has no version")
    version = version.group(1)
    commit = git(root, "rev-parse", "HEAD").strip()
    tag = "v" + version
    tagged = tag in git(root, "tag", "--points-at", "HEAD").split()
    dirty = bool(git(root, "status", "--porcelain", "--untracked-files=no").strip())
    files = crate_files(crate)
    return {
        "schema": 1,        "crate": "bovnar",
        "version": version,
        "repository": REPOSITORY,
        "commit": commit,
        "tag": tag if tagged else None,
        # True when the work tree had uncommitted changes: the files are then
        # not the commit's, and a publish refuses the crate.
        "dirty": dirty,
        "generators": build_rs_list(root, GENERATORS_RE),
        # The GitHub Actions artifact each c/prebuilt/<triple> came from: the job
        # that builds the library for that platform, in the run of this commit.
        "prebuilt": {rel.split("/")[2]: "bovnar-prebuilt-" + rel.split("/")[2]
                     for rel in files if rel.startswith("c/prebuilt/")},
        "files": {rel: {"sha256": sha256(os.path.join(crate, rel)),
                        "from": origin.get(rel)} for rel in files},
    }


def write_manifest(crate, data):
    with open(os.path.join(crate, MANIFEST), "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, sort_keys=True)
        f.write("\n")


def main():
    argv = sys.argv[1:]
    cargo_args = ["package"]
    if "--" in argv:
        i = argv.index("--")
        argv, cargo_args = argv[:i], argv[i + 1:] or cargo_args
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n\n")[0])
    ap.add_argument("--out", help="staging directory (default: a new temporary one)")
    ap.add_argument("--prebuilt",
                    help="directory of bovnar-prebuilt-<triple> artifacts from "
                         "the platform jobs (required to publish)")
    ap.add_argument("--no-cargo", action="store_true",
                    help="stage and check only; do not run cargo")
    args = ap.parse_args(argv)

    out = os.path.abspath(args.out) if args.out else tempfile.mkdtemp(prefix="bovnar-crate-")
    crate = os.path.join(out, "bovnar")
    publishing = not args.no_cargo and cargo_args[0] == "publish" \
        and "--dry-run" not in cargo_args
    # A published crate is immutable and every target it promised a prebuilt
    # archive for keeps that promise until the next release. The dry run is the
    # rehearsal of exactly that file, so it needs them too.
    if not args.no_cargo and cargo_args[0] == "publish" and not args.prebuilt:
        sys.exit("package.py: a publish needs --prebuilt DIR (the "
                 "bovnar-prebuilt-<triple> artifacts); see rust/build.rs PREBUILT")
    if os.path.exists(crate):
        shutil.rmtree(crate)
    inside = subprocess.run(["git", "-C", out, "rev-parse", "--is-inside-work-tree"],
                            capture_output=True, text=True).stdout.strip()
    if inside == "true":
        sys.exit("package.py: %s is inside a git work tree; pick a directory "
                 "outside it" % out)

    origin = stage(crate, prebuilt=args.prebuilt)
    data = provenance(crate, origin)
    write_manifest(crate, data)
    print(
        f"package.py: staged {crate} from {data['commit']}"
        f"{' (' + data['tag'] + ')' if data['tag'] else ''}"
        f"{', with uncommitted changes' if data['dirty'] else ''}",
        f"with {len(data['prebuilt'])} prebuilt "
        f"{'archive' if len(data['prebuilt']) == 1 else 'archives'}",
        flush=True)

    sys.path.insert(0, ROOT)
    import check_crate_provenance
    problems = check_crate_provenance.check(
        crate, allow_dirty=not publishing,
        tag=("v" + data["version"]) if publishing else None)
    if problems:
        sys.exit("package.py: the staged crate fails its provenance check:\n  "
                 + "\n  ".join(problems))
    print("package.py: provenance checked: the crate is commit %s, file for file"
          % data["commit"][:12], flush=True)

    if args.no_cargo:
        if not args.out:
            shutil.rmtree(out)
        return
    sys.exit(subprocess.run(["cargo"] + cargo_args, cwd=crate).returncode)


if __name__ == "__main__":
    main()
