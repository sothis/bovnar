#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""
check_rust_build.py — rust/build.rs builds the library CMake builds.

The Rust crate does not link a system libbvnr: rust/build.rs runs the table
generators and compiles the C sources itself, from lists of its own. They are a
second copy of CMakeLists.txt's BVNR_SOURCES_*, BVNR_PROFILE_NAMES and the
generator list, kept by hand. A C source added only to CMake shows up on the
Rust side as a link error, and only once the binding calls into it; a profile
added only to CMake is silently compiled with its #ifndef default.

This compares the three lists and fails on any difference. It also compares
the two numbers the crate restates from the C headers: BVNR_UNIT_STRING_MAX
(the unit-text buffers of ffi.rs and the test suite) and BVN_CURRENCY_FIRST
(where build.rs and enums.rs number the currencies from).

Usage:  python3 check_rust_build.py [repo-root]
Exit 0 when build.rs agrees with CMakeLists.txt, 1 (with the differences) when not.
"""
import os
import re
import sys


def cmake_sources(cmake):
    srcs = []
    for m in re.finditer(r'set\(BVNR_SOURCES_[A-Z]+\s+(.*?)\)', cmake, re.S):
        srcs += m.group(1).split()
    return srcs


def cmake_profiles(cmake):
    m = re.search(r'set\(BVNR_PROFILE_NAMES\s+(.*?)\)', cmake, re.S)
    return ["BVNR_WITH_%s_PROFILE" % p for p in m.group(1).split()] if m else []


def cmake_generators(cmake):
    # The generator scripts the table step runs, in the order CMake lists them.
    return re.findall(r'\$\{CMAKE_CURRENT_SOURCE_DIR\}/(gen_[a-z_]+\.py)', cmake)


def rust_const(rs, name):
    m = re.search(r'const %s: \[[^\]]+\] = \[(.*?)\];' % name, rs, re.S)
    return m.group(1) if m else ""


def rust_int(text):
    return int(text.replace("_", ""))


def constants(repo, problems):
    """(C name, C value, [(where, Rust value)]) for each restated constant."""
    def read(rel):
        with open(os.path.join(repo, rel), encoding="utf-8") as f:
            return f.read()
    out = []
    m = re.search(r'#define\s+BVNR_UNIT_STRING_MAX\s+(\d+)u?', read("include/bovnar.h"))
    want = int(m.group(1)) if m else None
    have = []
    for rel in ("rust/src/ffi.rs", "rust/tests/test_units.rs"):
        for v in re.findall(r'const UNIT_STRING_MAX: usize = ([\d_]+);', read(rel)):
            have.append((rel, rust_int(v)))
    out.append(("BVNR_UNIT_STRING_MAX", want, have))
    m = re.search(r'#define\s+BVN_CURRENCY_FIRST\s+(\d+)', read("include/bovnar_currency.gen.h"))
    want = int(m.group(1)) if m else None
    have = [("rust/src/enums.rs", rust_int(v)) for v in
            re.findall(r'pub const CURRENCY_FIRST: i32 = ([\d_]+);', read("rust/src/enums.rs"))]
    have += [("rust/build.rs", rust_int(v)) for v in
             re.findall(r'\b([\d_]+) \+ (?:i as i32|codes\.len\(\))', read("rust/build.rs"))]
    out.append(("BVN_CURRENCY_FIRST", want, have))
    for name, want, have in out:
        if want is None:
            problems.append("%s: not found in the C headers" % name)
        elif not have:
            problems.append("%s: no restatement found in rust/ (did the code move?)" % name)
        for where, v in have:
            if v != want:
                problems.append("%s: %s says %d, the C header %d" % (name, where, v, want))
    return out


def main():
    repo = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(repo, "CMakeLists.txt"), encoding="utf-8") as f:
        cmake = f.read()
    with open(os.path.join(repo, "rust", "build.rs"), encoding="utf-8") as f:
        rs = f.read()

    problems = []

    def compare(what, want, have):
        if not want:
            problems.append("%s: not found in CMakeLists.txt" % what)
        elif not have:
            problems.append("%s: not found in rust/build.rs" % what)
        elif want != have:
            for x in want:
                if x not in have:
                    problems.append("%s: rust/build.rs lacks %s" % (what, x))
            for x in have:
                if x not in want:
                    problems.append("%s: rust/build.rs has %s, CMakeLists.txt does not" % (what, x))
            if sorted(want) == sorted(have):
                problems.append("%s: same entries, different order" % what)

    compare("sources", cmake_sources(cmake),
            re.findall(r'"(src/[^"]+\.c)"', rust_const(rs, "SOURCES")))
    compare("profiles", cmake_profiles(cmake),
            re.findall(r'"(BVNR_WITH_[A-Z_]+_PROFILE)"', rust_const(rs, "PROFILES")))
    # Order matters here: gen_profiles.py reads what gen_units.py wrote.
    gens = list(dict.fromkeys(cmake_generators(cmake)))
    compare("generators", gens,
            re.findall(r'"(gen_[a-z_]+\.py)"', rust_const(rs, "GENERATORS")))

    consts = constants(repo, problems)

    if problems:
        for p in problems:
            print("FAIL  " + p)
        return 1
    print("OK: rust/build.rs compiles the %d sources, %d profile switches and "
          "runs the %d generators of CMakeLists.txt; %s agree with the C headers"
          % (len(cmake_sources(cmake)), len(cmake_profiles(cmake)), len(gens),
             ", ".join("%s (%d places)" % (n, len(h)) for n, _, h in consts)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
