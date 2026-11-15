# Bovnar 2.2.0 — a unit policy for the push-fed reader, and prebuilt C libraries

**A feature release of the 2.2 line: same format (spec 1.2), same C ABI.**
No C symbol was added or changed, so `libbvnr.so.2` keeps its soname and a
program built against 2.1.0 runs against 2.2.0 unchanged. The Python API gains
one method; the Rust crate's C half gains prebuilt libraries on three targets.

The two additions are the last of the 2.1 follow-ups. `DocumentFeed` was the
one live object with no unit policy of its own, and the crate needed a C
compiler on every target to build at all.

## Install

```bash
pip install --upgrade bovnar     # pure-ctypes bindings + bundled shared library
```

```toml
[dependencies]
bovnar = "2.2.0"                 # Rust; no C compiler needed on MSVC or macOS
```

Prebuilt archives for Linux, FreeBSD, macOS (universal, arm64 and x86_64) and
Windows (MinGW and MSVC), the single-file amalgamation and the source tarball
are attached to the GitHub release, with a `SHA256SUMS` file.

## Added — `DocumentFeed.set_unit_policy()`

The push-fed reader had no unit policy of its own. `Reader` and `Writer` have
`set_unit_policy`, and Rust's `live::DocumentFeed` always did, so a fed
document was the only way to read a stream with no policy at all — a document
carrying a unit the profile does not know was refused where the same document
read through a `Reader` with a policy would have been accepted.

`DocumentFeed.set_unit_policy(policy)` installs (or, with `None`, clears) the
policy of the reader behind the feed, exactly as `Reader.set_unit_policy` does:

```python
from bovnar import UnitPolicy, UnitRule
from bovnar.stream import DocumentFeed

feed = DocumentFeed()
feed.set_unit_policy(UnitPolicy(base="si", rules=[UnitRule("degC", "K")]))
```

doc/09 §4.7 and §5.6 name it. No C symbol was added, so the ABI is unchanged.

## Added — prebuilt C libraries in the Rust crate

`bovnar` on crates.io carried the C sources and compiled them in `build.rs`, so
it needed a C compiler on every target — including the two where a C toolchain
is the least convenient part of a Rust install. The crate now also carries the
static library the CI job for that platform already builds, in
`c/prebuilt/<target>/`, and links it:

- `x86_64-pc-windows-msvc`
- `x86_64-apple-darwin`
- `aarch64-apple-darwin`

Those three build with **no C compiler at all**. Everything else — Linux,
Windows GNU, FreeBSD, `wasm32-wasip2`, and any build with a unit-profile
feature off, the archives having all of them — compiles `c/` as before, and
`BOVNAR_BUILD_FROM_SOURCE=1` forces that path everywhere.

`PROVENANCE.json` names each archive's SHA-256 and the artifact it came from,
and a new `rust_prebuilt` CI job links and tests the packaged crate on those
targets, so a prebuilt library cannot silently drift from the sources.

## Fixed

The release carries the fixes from the passes over the tree before the tag,
each checked against the code before it was fixed, with a regression test where
one can be written:

- **The Linux MinGW cross-build links the Universal CRT**, matching the archive
  CI builds with MSYS2 UCRT64 (`api-ms-win-crt-*.dll`, not `msvcrt.dll`).
- **A unit-shaped argument of the wrong type, and `convert_rational(True)`.**
  A `str` unit reached ctypes and raised `ctypes.ArgumentError` naming neither
  the argument nor the type wanted; one door (`_as_vu`) now answers
  `TypeError: expected a Unit or a ValueUnit, not str`.
- **`canonicalize` when both sides fail** now reports the sink's failure first,
  as Rust does.
- **`DomNode.as_int_str()`** reads the pointer as bytes.
- **A `float:32` or `float:16` subnormal printed digits it does not hold** —
  `2**-149` under `float:32` is `1.4e-45` now, not `1.4012985e-45`.
- **`harness_fd_stream` no longer trusts `tmpfile()`.** This is a test-harness
  fix, not a library one, and worth stating because the symptom looked like a
  library bug: under Wine, concurrent processes had `tmpfile()` fail outright
  7 times in 8, and the file that does come back is not reliably private, so
  the fd sink's document was read back as some other iteration's and
  `readback_validate` correctly refused it. `push_fd`'s counter is exact — the
  fd position equalled `bvnr_writer_bytes_written()` in every trapped run — so
  the library was never at fault. The harness uses one `mkstemp()` file per
  process, truncated and rewound per iteration.

The full list is in [CHANGELOG.md](CHANGELOG.md) under 2.2.0.

## Verification

`./run_tests.sh` — the full build, every registered CTest test and the fuzz
sweep — the ASan+UBSan build, the Rust crate's `cargo test --locked`,
`cargo clippy -- -D warnings`, `cargo fmt --check`, the Python wheel, the
WASM freshness and web gates, and the Wine cross tree were run on the release
tree. The numbers are in the release commit, and the final tree was re-verified
before the tag.