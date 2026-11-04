# Bovnar 2.0.1 — exact long literals, and a macOS archive

**A patch release of the 2.0 line: same format, same ABI, same APIs.** It
implements spec 1.2, as 2.0.0 does; `libbvnr.so` stays at SOVERSION 2, and the
public C ABI is now checked against 2.0.0's on every build. Programs built
against 2.0.0 run unchanged. What changes is that a few very long or extreme
float literals now read to the value they denote, and that the release carries
a macOS build.

## Install

```bash
pip install --upgrade bovnar     # pure-ctypes bindings + bundled shared library
```

```toml
[dependencies]
bovnar = "2.0.1"                 # Rust; compiles the C library in (needs a C compiler)
```

Prebuilt archives for Linux, macOS (universal, arm64 and x86_64) and Windows
(MinGW and MSVC), the single-file amalgamation and the source tarball are
attached to the GitHub release, with a `SHA256SUMS` file.

## Fixed

- **Long decimal literals round correctly at any length.** Past its digit
  budget the parser could not tell a literal from a rounding midpoint whose own
  expansion was longer than the digits it kept, and could round to the wrong
  neighbour: binary128 midpoints run to about 11 500 digits. When digits are
  dropped, the result is now checked against the exact decimal expansions of the
  midpoints around it. Against an exact rational oracle on 1 017 literals, the
  previous code rounded 141 wrongly; this one rounds none.
- **Hex text is read exactly at any precision and exponent.** `bvn_float` in
  base 16 saturated to inf or 0 past a binary exponent of about ±32 640 and at
  precisions near its ceiling, including on hex it had written itself. It now
  reads the digits' bits directly and is exact up to a binary exponent of ±10^9;
  what `bvn_float_to_str` writes in base 16 reads back.
- **A long decimal literal at a small magnitude keeps its range.** At a
  precision above 53 a long literal read as 0 from about 1e-6165; it now reads
  down to about 1e-9810 at precision 113.

Ordinary literals — up to the digit budget, at everyday magnitudes — read as
they did. Only inputs at these edges change value, and each now reads as the
correctly rounded value the specification requires.

## Added

- **A macOS release archive**, `bovnar-macos-<version>-<count>.tar.xz`: the
  library (static and shared) and the CLI as universal binaries for macOS 11
  and later, built natively in CI, with both slices and the deployment target
  checked in every binary.
- **An ABI baseline.** `tests/abi/` holds the public C ABI as released in
  2.0.0 — struct sizes and offsets, enum values, constants, prototypes and the
  library's exports — and the `bvnr_abi_baseline` gate fails on any drift.
- **The Rust tests run on wasm32-wasip2**, under wasmtime in CI, which covers
  the crate's 32-bit `usize` paths.

## Changed

- **The MinGW Windows archive links the Universal CRT.** It is built in MSYS2
  UCRT64 instead of the deprecated MINGW64, so `libbvnr.dll` and `bovnar.exe`
  import `ucrtbase.dll` (part of Windows 10 and later, as for the MSVC archive)
  instead of `msvcrt.dll`. Its static `libbvnr.a` is for a UCRT MinGW
  toolchain; link against the DLL, or build from source, with an msvcrt one.
- **The Linux archive and the wheels build on current runners**
  (`ubuntu-26.04`, `ubuntu-26.04-arm`, `macos-26`), pinned so an image changes
  only by an edit.
- **The Rust crate is published by CI** through crates.io Trusted Publishing,
  from the release tag.

The full list is in [CHANGELOG.md](CHANGELOG.md) under 2.0.1.
