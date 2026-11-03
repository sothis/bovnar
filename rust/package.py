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
        c/include/        include/*.h
        c/src/            the C sources build.rs compiles, and every src/ header
        c/generated/      the generators' output (*.gen.inc)

build.rs sees c/generated/ and builds from c/ without running anything, so the
published crate needs a C compiler but no Python. tests/ and fuzz/ stay out:
they read ../examples and ../src/gendata, which only the repository has, and
the repository's CI runs them.

The staging directory must not be inside a git work tree (cargo would apply
that repository's .gitignore to the package), so the default is a fresh
temporary directory.

Usage:
    python3 rust/package.py [--out DIR] [-- CARGO-ARGS...]

CARGO-ARGS default to `package`, which builds the .crate and verifies it by
compiling it on its own. Other examples:
    python3 rust/package.py -- publish --dry-run
    python3 rust/package.py -- publish            # needs a crates.io token
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUST = os.path.join(ROOT, "rust")
GENERATORS_RE = re.compile(r"const GENERATORS: \[[^\]]+\] = \[(.*?)\];", re.S)
SOURCES_RE = re.compile(r"const SOURCES: \[[^\]]+\] = \[(.*?)\];", re.S)
PUBLISH_RE = re.compile(r"^# Not from here:.*?^publish = false\n", re.S | re.M)


def tracked(prefix):
    out = subprocess.run(["git", "ls-files", "-z", "--", prefix], cwd=ROOT,
                         check=True, capture_output=True).stdout
    return [p for p in out.decode().split("\0") if p]


def copy(rel, dest_root, dest_rel=None):
    dest = os.path.join(dest_root, dest_rel or rel)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    shutil.copy2(os.path.join(ROOT, rel), dest)


def build_rs_list(regex):
    with open(os.path.join(RUST, "build.rs"), encoding="utf-8") as f:
        m = regex.search(f.read())
    if not m:
        sys.exit("package.py: cannot find %s in rust/build.rs" % regex.pattern[:20])
    return re.findall(r'"([^"]+)"', m.group(1))


def stage(crate):
    # The crate itself, minus tests/ and fuzz/.
    for rel in tracked("rust"):
        sub = rel[len("rust/"):]
        if sub.startswith(("tests/", "fuzz/")):
            continue
        copy(rel, crate, sub)
    for rel in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        copy(rel, crate)

    toml = os.path.join(crate, "Cargo.toml")
    with open(toml, encoding="utf-8") as f:
        text = f.read()
    text, n = PUBLISH_RE.subn("", text)
    if n != 1:
        sys.exit("package.py: rust/Cargo.toml no longer has the `publish = false` "
                 "block this script lifts; update PUBLISH_RE")
    with open(toml, "w", encoding="utf-8") as f:
        f.write(text)

    # The generated tables, made the way build.rs makes them in the repository.
    gen = os.path.join(crate, "c", "generated")
    os.makedirs(gen)
    env = dict(os.environ, BVNR_GENERATED_DIR=gen, PYTHONDONTWRITEBYTECODE="1")
    for script in build_rs_list(GENERATORS_RE):
        subprocess.run([sys.executable, os.path.join(ROOT, script)],
                       cwd=ROOT, env=env, check=True, stdout=subprocess.DEVNULL)
    # The generators also refresh the committed include/*.gen.h. If that
    # changed anything, the headers in git are stale and so would the crate be.
    stale = subprocess.run(["git", "diff", "--name-only", "--", "include"],
                           cwd=ROOT, check=True, capture_output=True, text=True).stdout
    if stale.strip():
        sys.exit("package.py: the generators changed committed headers:\n" + stale
                 + "commit them first, so the crate matches the repository")

    # The C sources build.rs compiles, and every header under include/ and src/.
    sources = set(build_rs_list(SOURCES_RE))
    for rel in tracked("include") + tracked("src"):
        if rel.startswith("src/gendata/"):
            continue
        if rel.endswith(".h") or rel in sources:
            copy(rel, os.path.join(crate, "c"))
    missing = [s for s in sources if not os.path.exists(os.path.join(crate, "c", s))]
    if missing:
        sys.exit("package.py: build.rs names sources git does not track: %s" % missing)


def main():
    argv = sys.argv[1:]
    cargo_args = ["package"]
    if "--" in argv:
        i = argv.index("--")
        argv, cargo_args = argv[:i], argv[i + 1:] or cargo_args
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", help="staging directory (default: a new temporary one)")
    args = ap.parse_args(argv)

    out = os.path.abspath(args.out) if args.out else tempfile.mkdtemp(prefix="bovnar-crate-")
    crate = os.path.join(out, "bovnar")
    if os.path.exists(crate):
        shutil.rmtree(crate)
    inside = subprocess.run(["git", "-C", out, "rev-parse", "--is-inside-work-tree"],
                            capture_output=True, text=True).stdout.strip()
    if inside == "true":
        sys.exit("package.py: %s is inside a git work tree; pick a directory "
                 "outside it" % out)

    stage(crate)
    print("package.py: staged %s" % crate, flush=True)
    sys.exit(subprocess.run(["cargo"] + cargo_args, cwd=crate).returncode)


if __name__ == "__main__":
    main()
