#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""
check_abi.py — the public C ABI, against a committed baseline.

The binding tests (python/tests/test_abi.py, abi_tests in rust/src/ffi.rs)
check that the Python and Rust mirrors agree with the C headers. They cannot
see a change to the headers themselves: grow a struct, renumber an enum, drop
an export, update both mirrors, and every test passes while a program compiled
against the previous release reads the wrong offsets from the same soname.

This compares what the headers and the library say the ABI is with
tests/abi/bvnr-<major>.abi and .syms, <major> being BVNR_ABI_MAJOR (the
SOVERSION) in CMakeLists.txt. Any difference fails. It records:

  size / offset  every public struct with a body: its size, and each member's
                 offset and size
  enum           every enum constant's value
  const          every integer object-like macro (array bounds, flag bits, id
                 ranges); the version macros, which move every release, are
                 left out
  func           every function prototype, without parameter names
  (.syms)        the shared library's exported symbols (ELF only)

Nothing is listed by hand. The public headers are run through the C
preprocessor and parsed, so a new struct, member or constant is in the dump
without anyone remembering it; the values come from compiling and running a
program generated from those names, so they are the compiler's and not the
parser's.

An intended change is made by running with --update and committing the new
baseline with the change, which turns it into a decision. Additions are
compatible. Anything removed or changed that a compiled caller depends on is a
break: bump BVNR_ABI_MAJOR, which starts a new baseline file.

Usage:
  python3 check_abi.py [--cc CC] [--lib libbvnr.so] [--nm NM] [--update]

The baseline is for 64-bit targets built by GCC or Clang, where the CTest gate
runs; the export list is read with `nm -D` and so only checked on ELF.
"""
import argparse
import difflib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
INCLUDE = ROOT / "include"
ABI_DIR = ROOT / "tests" / "abi"

# Moved by every release, and not an ABI property: the soname is.
SKIP_MACRO_RX = re.compile(r"^BVNR_(SPEC_)?VERSION")
TOKEN_RX = re.compile(r"\.\.\.|\w+|[^\s\w]")
IDENT_RX = re.compile(r"[A-Za-z_]\w*$")
# Words that end a parameter's type rather than name the parameter, so
# `unsigned` or `const char *` is not mistaken for a named one.
TYPE_WORDS = {"void", "char", "short", "int", "long", "float", "double",
              "signed", "unsigned", "_Bool", "const", "volatile", "restrict",
              "struct", "union", "enum"}
QUALIFIERS = {"const", "volatile", "restrict", "struct", "union", "enum"}


def abi_major():
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    m = re.search(r"set\(BVNR_ABI_MAJOR\s+(\d+)\)", text)
    if not m:
        sys.exit("check_abi: BVNR_ABI_MAJOR not found in CMakeLists.txt")
    return int(m.group(1))


def public_headers():
    # The .gen.h files are fragments that bovnar.h includes inside its enums.
    return sorted(p.name for p in INCLUDE.glob("*.h") if ".gen." not in p.name)


def preprocess(cc, tmp):
    umbrella = tmp / "umbrella.c"
    umbrella.write_text("".join(f'#include "{h}"\n' for h in public_headers()))
    out = subprocess.run([cc, "-std=c99", "-E", "-dD", "-I", str(INCLUDE),
                          str(umbrella)],
                         capture_output=True, text=True, check=True).stdout
    # Keep only what the public headers themselves declare, not the system
    # headers they include.
    code, macros, public = [], [], False
    for line in out.splitlines():
        m = re.match(r'#\s*\d+\s+"([^"]*)"', line)
        if m:
            path = Path(m.group(1))
            if not path.is_absolute():
                path = (tmp / path) if (tmp / path).exists() else (ROOT / path)
            public = path.resolve().parent == INCLUDE
            continue
        if not public:
            continue
        if line.startswith("#define "):
            macros.append(line[len("#define "):])
        elif not line.startswith("#"):
            code.append(line)
    return "\n".join(code), macros


def strip_attributes(text):
    """Remove __attribute__((...)), balanced, which BVN_API expands to."""
    out, i = [], 0
    while True:
        j = text.find("__attribute__", i)
        if j < 0:
            out.append(text[i:])
            return "".join(out)
        out.append(text[i:j])
        k = text.index("(", j)
        depth = 0
        while True:
            depth += {"(": 1, ")": -1}.get(text[k], 0)
            k += 1
            if depth == 0:
                break
        i = k


def split_top(text, sep):
    """Split at `sep` where no (), [] or {} is open.

    A function body (the static inline helpers) ends without a `;`, so its
    closing brace ends the declaration too."""
    parts, depth, start, fn_body = [], 0, 0, False
    for i, c in enumerate(text):
        if c in "([{":
            if c == "{" and depth == 0:
                fn_body = text[:i].rstrip().endswith(")")
            depth += 1
        elif c in ")]}":
            depth -= 1
            if c == "}" and depth == 0 and fn_body and sep == ";":
                parts.append(text[start:i + 1])
                start = i + 1
        elif c == sep and depth == 0:
            parts.append(text[start:i])
            start = i + 1
    parts.append(text[start:])
    return [p.strip() for p in parts if p.strip()]


def brace_body(decl):
    """(before, body, after) around the first top-level {...}, or None."""
    i = decl.find("{")
    if i < 0:
        return None
    depth = 0
    for k in range(i, len(decl)):
        depth += {"{": 1, "}": -1}.get(decl[k], 0)
        if depth == 0:
            return decl[:i], decl[i + 1:k], decl[k + 1:]
    raise ValueError(f"unbalanced braces in: {decl[:80]}")


def drop_bodies(text):
    """`union { ... } _reserved` -> `union _reserved`."""
    while (parts := brace_body(text)) is not None:
        text = parts[0] + " " + parts[2]
    return text


def declarator_name(decl):
    m = re.search(r"\(\s*\*\s*(\w+)\s*\)", decl)
    if m:                       # function pointer
        return m.group(1)
    decl = re.sub(r"(\[[^\]]*\])+\s*$", "", decl)   # array bounds
    decl = re.sub(r":\s*\d+\s*$", "", decl)          # bit-field width
    return IDENT_RX.search(decl.strip()).group(0)


def member_names(body):
    names = []
    for member in split_top(body, ";"):
        member = drop_bodies(member)
        decls = split_top(member, ",")
        names.append(declarator_name(decls[0]))
        names += [declarator_name(d) for d in decls[1:]]
    return names


def normalise_param(param):
    """Drop the parameter's name: the prototype's ABI is its types."""
    fp = re.match(r"(.*\(\s*\*)\s*\w*\s*(\)\s*)\((.*)\)\s*$", param, re.S)
    if fp:                      # function pointer: its own parameters too
        inner = ", ".join(normalise_param(p) for p in split_top(fp.group(3), ","))
        return " ".join(TOKEN_RX.findall(f"{fp.group(1)}{fp.group(2)}({inner})"))
    arr = re.search(r"(\[[^\]]*\])+\s*$", param)
    toks = TOKEN_RX.findall(param[:arr.start()] if arr else param)
    if (len(toks) >= 2 and IDENT_RX.match(toks[-1])
            and toks[-1] not in TYPE_WORDS
            and any(IDENT_RX.match(t) and t not in QUALIFIERS
                    for t in toks[:-1])):
        toks.pop()
    return " ".join(toks + (TOKEN_RX.findall(arr.group(0)) if arr else []))


def parse(code):
    """Structs (name -> members), enum constants, and prototypes, in order."""
    code = strip_attributes(code)
    tag_to_typedef = {}
    structs, enums, funcs = [], [], []
    for decl in split_top(code, ";"):
        decl = " ".join(decl.split())
        m = re.match(r"typedef (?:struct|union) (\w+) (\w+)$", decl)
        if m:
            tag_to_typedef[m.group(1)] = m.group(2)
            continue
        if decl.startswith("static"):
            continue
        parts = brace_body(decl)
        if parts:
            head, body, tail = parts
            words = head.split()
            if "enum" in words:
                for item in split_top(body, ","):
                    enums.append(re.match(r"\w+", item).group(0))
            elif "struct" in words or "union" in words:
                tag = words[-1] if words[-1] not in ("struct", "union") else None
                name = tail.strip() if words[0] == "typedef" else tag
                structs.append((name, tag, member_names(body)))
            continue
        if "(" in decl and not decl.startswith(("typedef", "static")):
            funcs.append(decl)
    # A struct defined by tag, typedef'd elsewhere, is reached by the typedef.
    structs = [(tag_to_typedef.get(tag, name) if name == tag else name, members)
               for name, tag, members in structs]
    return structs, enums, funcs


def prototype(decl):
    toks = TOKEN_RX.findall(decl)
    i = toks.index("(")
    name, ret = toks[i - 1], " ".join(toks[:i - 1])
    inner = decl[decl.index("(") + 1:decl.rindex(")")]
    params = ", ".join(normalise_param(p) for p in split_top(inner, ","))
    return name, f"func {name} {ret} ({params})"


def integer_macros(macros):
    out = []
    for m in macros:
        name, _, value = m.partition(" ")
        if ("(" in name or not value.strip() or SKIP_MACRO_RX.match(name)
                or name.endswith("_H_") or name.endswith("_H")
                or any(c in value for c in '"{') or "__attribute__" in value):
            continue
        out.append(name)
    return out


def dump_layout(cc, tmp, structs, enums, consts):
    """Compile a program that prints what the compiler makes of the names."""
    lines = ["#include <stddef.h>", "#include <stdio.h>"]
    lines += [f'#include "{h}"' for h in public_headers()]
    lines.append("int main(void)\n{")
    for name, members in structs:
        lines.append(f'\tprintf("size {name} %zu\\n", sizeof({name}));')
        for f in members:
            lines.append(f'\tprintf("offset {name}.{f} %zu %zu\\n", '
                         f'offsetof({name}, {f}), sizeof((({name} *)0)->{f}));')
    for e in enums:
        lines.append(f'\tprintf("enum {e} %lld\\n", (long long)({e}));')
    for c in consts:
        lines.append(f'\tprintf("const {c} %lld\\n", (long long)({c}));')
    lines.append("\treturn 0;\n}\n")
    src, exe = tmp / "abi_layout.c", tmp / "abi_layout"
    src.write_text("\n".join(lines))
    subprocess.run([cc, "-std=c99", "-I", str(INCLUDE), str(src), "-o", str(exe)],
                   capture_output=True, text=True, check=True)
    return subprocess.run([str(exe)], capture_output=True, text=True,
                          check=True).stdout.splitlines()


def exported_symbols(nm, lib):
    out = subprocess.run([nm, "-D", "--defined-only", str(lib)],
                         capture_output=True, text=True, check=True).stdout
    # Sanitizer and loader internals start with an underscore; the API never does.
    names = {ln.split()[-1] for ln in out.splitlines() if ln.split()}
    return sorted(n for n in names if not n.startswith("_"))


def compare(path, now, update):
    if update:
        path.parent.mkdir(exist_ok=True)
        path.write_text("".join(f"{ln}\n" for ln in now))
        print(f"check_abi: wrote {path.relative_to(ROOT)} ({len(now)} lines)")
        return True
    if not path.exists():
        print(f"check_abi: no baseline {path.relative_to(ROOT)} for this ABI "
              f"major; create it with --update")
        return False
    base = path.read_text(encoding="utf-8").splitlines()
    if base == now:
        return True
    diff = list(difflib.unified_diff(base, now, str(path.relative_to(ROOT)),
                                     "this build", lineterm="", n=0))
    removed = sum(1 for d in diff if d.startswith("-") and not d.startswith("---"))
    added = sum(1 for d in diff if d.startswith("+") and not d.startswith("+++"))
    print("\n".join(diff))
    print(f"\ncheck_abi: the public ABI differs from {path.relative_to(ROOT)}: "
          f"{removed} line(s) removed or changed, {added} added.")
    if removed:
        print("  A removed or changed line breaks a program compiled against the "
              "baseline if it\n  depends on it: if so, bump BVNR_ABI_MAJOR in "
              "CMakeLists.txt and start\n  a new baseline. Reclaiming a _reserved "
              "slot is the usual compatible exception.")
    print("  An intended change: run `python3 check_abi.py --update` and commit "
          "the baseline\n  with it, and add a line to CHANGELOG.md.")
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--cc", default=os.environ.get("CC") or "cc")
    ap.add_argument("--lib", help="libbvnr.so whose exports to check")
    ap.add_argument("--nm", default="nm")
    ap.add_argument("--update", action="store_true",
                    help="write the baseline instead of comparing against it")
    args = ap.parse_args()
    if args.lib is None and (ROOT / "build" / "libbvnr.so").exists():
        args.lib = str(ROOT / "build" / "libbvnr.so")
    if not shutil.which(args.cc):
        sys.exit(f"check_abi: no C compiler {args.cc!r}")

    major = abi_major()
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        try:
            code, macros = preprocess(args.cc, tmp)
            structs, enums, funcs = parse(code)
            layout = dump_layout(args.cc, tmp, structs, enums,
                                 integer_macros(macros))
        except subprocess.CalledProcessError as e:
            sys.exit(f"check_abi: {' '.join(map(str, e.cmd))} failed:\n{e.stderr}")

    protos = dict(prototype(f) for f in funcs)
    abi = layout + [protos[n] for n in sorted(protos)]
    ok = compare(ABI_DIR / f"bvnr-{major}.abi", abi, args.update)

    if args.lib:
        syms = exported_symbols(args.nm, args.lib)
        # An export the headers do not declare is unreachable API; a
        # declaration the library does not export is a link error waiting
        # for the first caller.
        undeclared = sorted(set(syms) - set(protos))
        unexported = sorted(set(protos) - set(syms))
        for what, names in (("exported but not declared in include/",
                             undeclared),
                            ("declared in include/ but not exported",
                             unexported)):
            if names:
                ok = False
                print(f"check_abi: {what}: {' '.join(names)}")
        ok = compare(ABI_DIR / f"bvnr-{major}.syms", syms, args.update) and ok
    else:
        print("check_abi: no --lib; the export list was not checked")

    if ok and not args.update:
        n_struct = sum(1 for ln in abi if ln.startswith("size "))
        print(f"check_abi: ABI {major} matches the baseline ({n_struct} structs, "
              f"{sum(1 for ln in abi if ln.startswith('enum '))} enum constants, "
              f"{len(protos)} functions"
              + (f", {len(syms)} exports)" if args.lib else ")"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
