# Bovnar 2.1.0 — live streams in C, Rust and Python

**A feature release of the 2.1 line: same format (spec 1.2), same C ABI.**
Every C export is an addition, so `libbvnr.so.2` keeps its soname and a
program built against 2.0.x runs against 2.1.0 unchanged. The C library's
observable changes are the corrected canonical text of a double and the fixes
below.

The Python API is additive; argument and re-entrancy checks are stricter in
three places — an fd must be a real `int` in C `int` range, `write_bool` takes
a `bool`, and the writer's `set_unit_policy` refuses a call from its own sink
or observer.
The Rust crate has one **source-incompatible** change: `ReadOptions` gained
`max_text_bytes` and `max_array_items`, so a struct literal naming every field
must add `..ReadOptions::default()` (code using `..Default::default()` or the
defaults is unaffected).

The headline is that a stream no longer has to end before it is read or
written. A document, a `BVF1` frame stream and a multiplexed octet stream can
now be handed over in pieces of any size — a socket, a pipe, an event loop —
and parsed as the bytes arrive; a writer can push each message out before the
next is sent. Both bindings wrap the same C state machines, and the pure-Rust
codec implements the wire conventions without a C call per message.

## Install

```bash
pip install --upgrade bovnar     # pure-ctypes bindings + bundled shared library
```

```toml
[dependencies]
bovnar = "2.1.0"                 # Rust; compiles the C library in (needs a C compiler)
```

Prebuilt archives for Linux, FreeBSD, macOS (universal, arm64 and x86_64) and
Windows (MinGW and MSVC), the single-file amalgamation and the source tarball
are attached to the GitHub release, with a `SHA256SUMS` file. The FreeBSD
archive is new with this release, built with the base clang and needing no
libm; it passes the same CLI smoke test as the other platform archives.

## Added — push-fed reading and writing

- **C:** `bvnr_open_read_feed` / `bvnr_read_feed` / `bvnr_read_finish` parse a
  document from bytes handed over as they arrive; `bvnr_frame_reader_*` does
  the same for `BVF1` frames; `bvnr_demux_*_feed` for a multiplexed stream.
  `bvnr_source_from_callback` / `bvnr_sink_to_callback` build sources and sinks
  from the caller's own callbacks with a user pointer, and
  `bvnr_write_flush` pushes buffered output between any two writer calls.
  Running out of input is never a feed error; `finish` reports truncation.
- **Rust:** `bovnar::codec` writes and reads the octet multiplexing and frames
  incrementally in pure Rust (`#![forbid(unsafe_code)]`, no allocation per
  message once warm), with borrowed payloads and peek/skip for forwarders;
  `bovnar::live` wraps the C feeds as `DocumentFeed`, `ValueFeed`,
  `FrameParser`, `FramePull`, `Demux` and `EmbeddedFeed`. The crate's
  `std::io` entry points now parse as they read and write 64 KiB blocks as
  they serialise instead of buffering the whole document.
- **Python:** `stream.DocumentFeed`, `FrameFeed` and `DemuxFeed`; sockets and
  binary file objects as sources and sinks (`Reader.read_stream`,
  `Writer.to_stream`, `load`/`dump`, and a `file=`/socket form of the stream
  helpers); live writing (`stream.MuxWriter`, `stream.FrameWriter`,
  `Writer.flush`); and `mux_dump(..., preamble={...})` for metadata before a
  stream.

## Added — everything else the bindings could not reach

- The complete reader-side C API reaches the WASM module: options objects with
  every read flag and nesting limit, `createPolicy()` for the unit policy,
  `octets: 'hex'` for binary payloads, the stateless unit/version helpers, and
  `parseTypeAnnotation()`. The multi-file and single-file builds are held to
  the same answers.
- **CLI:** `bovnar bench --stream` measures the streaming paths beside the
  parse table — raw octet streams, mux send and demux, the push-fed demux and
  framed documents — with a wall-clock budget per cell (`--stream-seconds`)
  and `--stream-only`.
- Civil time, calendars and epochs (`bovnar.timekeeping` /
  `bovnar::timekeeping`), the literal helpers and type-annotation parser, the
  DOM builder, exact unit conversion (`convert_rational`), and the writer's
  `on_event` observer and nesting caps.
- **FreeBSD 15.1** builds and passes the C suite with the base clang, without
  libm; the WASM module builds with the ports emscripten.

## Changed — faster, and one meaning for every read option

- Every read flag now reaches every reading entry point in Rust and Python:
  `max_text_bytes`, `max_array_items`, the five length caps and both nesting
  caps, with `0` keeping the library default. `on_error` is on the `Reader`
  methods and `DocumentFeed`.
- `bvn_format_double` now writes the nearest shortest decimal (it could pick
  the other candidate for values just above a midpoint), and Python writes
  array floats in the same canonical form as scalar floats and the Rust crate.
- Re-opening a reader allocates nothing; `bvnr_mux_send` allocates nothing per
  call; a demux message that lies whole in a chunk is handed out without a
  copy; the demux channel lookup is constant-time; the Rust crate's memory
  writer hands its buffer out instead of copying it. The figures are in
  doc/10 §8.4, doc/09 §4.7 and doc/14 §4.8–4.9.

## Fixed

The release carries the fixes from five review passes over the tree before
the tag, each checked against the code before it was fixed (and reproduced
where it is reachable), with a regression test where one can be written: a
hang from a 14-byte hexadecimal float, a truncated JSON object
accepted by `bovnar convert`, NULL dereferences in the writer helpers,
subnormal float rendering, sign handling in base 64/85 integers, IEEE
geometry bounds, re-opened writers leaking a rescale, `finish()` forgetting
its error in both bindings, the WASM translation of truncated streams and
explicit `no_unit`, policy handles escaping one WASM instance into another,
a demux channel left counted when its hash allocation failed, and the
libm-free guarantee on FreeBSD. The full list is in
[CHANGELOG.md](CHANGELOG.md) under 2.1.0.

## Verification

`./run_tests.sh` — the full build, every registered CTest test and the fuzz
sweep — the ASan+UBSan build, the Rust crate's `cargo test --locked`,
`cargo clippy -- -D warnings`, `cargo fmt --check` and the 1.88 MSRV, the
Python wheel, and the WASM freshness and web gates were run on the release
tree. The numbers are in the release commit, and the final tree was re-verified
before the tag.
