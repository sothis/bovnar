# TODO

## State 2026-11-09 — main is ahead of 2.0.2

main carries the unreleased 2.1.0 work (CHANGELOG, Unreleased): the streaming
layer for both bindings and the binding plumbing, with three review passes
fixed (the last on 2026-11-09, CHANGELOG Fixed, "second pass"). The tree
still says 2.0.2 (`-dev` in the CLI), and the web pages on main already
describe the 2.1.0 API: publish the site after the tag, not before.

What the 2.1.0 release commit has to move (nothing gates the doc lines):
CMakeLists.txt `project(VERSION)`; `bovnar.h` version macros, with
`BVNR_VERSION_SUFFIX` back to `""`; pyproject.toml (version and the
cibuildwheel test command); `python/bovnar/__init__.py` `__version__`;
`python/tests/test_version.py`; rust/Cargo.toml, rust/Cargo.lock,
rust/fuzz/Cargo.lock; `rust/tests/test_version.rs`; CITATION.cff `version`
and `date-released`; RELEASE_NOTES_v2.1.0.md (the release job fails without
it); SECURITY.md's supported-versions table; the CHANGELOG heading and links;
rust/README.md and the "2.0.2" lines in doc/02, doc/03, doc/09, doc/11 and
doc/14; the IETF draft date and the doc/wiki infoboxes.

## State 2026-11-04 — 2.0.2 released

`v2.0.2` is tagged on b374191 and published by CI: the GitHub release (seven
assets, build count 806), PyPI, and crates.io through Trusted Publishing. Its
crate is the first with `PROVENANCE.json`; the copy on crates.io was checked
against the tag with `check_crate_provenance.py` (105 files, clean). The first
tag run failed on the new CI provenance step (it looked for the .crate where
`cargo package`, not `cargo publish --dry-run`, leaves it); nothing had been
published, so the step was fixed and the unpublished tag moved onto the fix.

Zenodo 2.0.2: source 10.5281/zenodo.23963398, documentation and specification
10.5281/zenodo.23963399 (CITATION.cff, README); `PRODUCTION_RECORDS` names
them for the next release. The deposit's doc/ check now leaves out
doc/wiki and doc/joss_paper, which no PDF uses.

Downstream: Mumcord (github.com/Mumcord/mumcord) uses the crate and ties its
licence notices to `PROVENANCE.json` (their PR #486, merged).

A Build & Package run that fails only because GitHub found no runner (the macOS
shortage that cost 2.0.1 five attempts) is now re-run by the `runner-shortage`
workflow: after five minutes, up to attempt 50, only for push runs whose branch
has not moved on (tags always). `.github/scripts/runner-shortage.sh REPO RUN
[ATTEMPT]` prints its verdict for any run. It takes effect from its first push
to main; its first real use is the evidence it works.

## Open 2026-11-09 — what the binding review left to the maintainer

The review of the binding work (a7ba940..6ff3ed8; fixes in baa4eb7) found
these. Each is a design choice rather than a bug: the behaviour is documented
or harmless, and changing it means picking a side. None blocks 2.1.0.

1. **Feed callbacks differ between the bindings.** Rust's `FrameParser`
   (`feed_with`) and `live::Demux` (`next_with`/`finish_with`) take
   `ReadHandlers` (`on_error`, `want_unit`, `on_unverified`) on each call.
   Python's `FrameFeed` and `DemuxFeed` take only the `READ_OPTIONS`, as do
   `load_documents`, `mux_load`, `parse_embedded`, `loads`/`load` and
   `canonicalize`; only `Reader.read_*` and `DocumentFeed` take the callbacks.
   Either add `on_error`/`want_unit`/`on_unverified` to the Python feeds, or
   say in doc/09 that they are for `DocumentFeed` and `Reader` only. The other
   way round, Python's `FrameFeed(return_errors=True)` has a Rust equivalent
   only in batch form (`stream::load_documents_results`).
2. **Writer limits are not bound.** `WriteOptions` (Rust) and the `Writer.to_*`
   keywords (Python) expose the nesting caps and `on_event`, but not the five
   length caps, `max_array_items`, `max_text_bytes`, `max_file_size`,
   `continue_on_error` or `on_error` of `bvnr_write_flags_t`. The read side now
   binds every flag. `dumps`/`dump` expose neither `on_event` nor the nesting
   caps.
3. **Size-cap defaults and meanings disagree.** `Reader.read_file` defaults to
   `max_file_size=16 MiB` (`MAX_FILESIZE_BYTES`); `read_mem`, `read_fd`,
   `read_stream`, `loads`, `load` and the stream functions default to 0, which
   is no cap. `DomDoc.parse_fd`/`parse_file(max_bytes=0)` and Rust's
   `parse_reader_capped(0)` mean the 256 MiB DOM cap, not unlimited.
   `max_file_size` caps one document in `load_documents`/`FrameFeed` but the
   whole stream in `mux_load`/`DemuxFeed` (documented). One meaning for 0, and
   one default, would be easier to explain; either is a behaviour change.
4. **A refused DOM builder node.** When `DomDoc::add` refuses a key (a
   duplicate, a non-identifier), Rust destroys the node; Python leaves it with
   the caller, who may add it elsewhere. Rust's `add` returns `()`, Python's the
   new `DomNode`; Rust's duplicate-key error carries `DuplicateStructKey`,
   Python's is a plain `BovnarArgumentError`. Align on one model.
5. **Python error types for a wrong argument type are mixed.**
   `BovnarArgumentError` in timekeeping, `datetime_epoch_index`,
   `write_datetime` and the read options; `TypeError` in the literal helpers,
   `parse_type_annotation`, `char_to_digit` and the DOM builder; a raw
   `ctypes.ArgumentError` from `convert_rational`/`convert_value` given a `str`
   unit (and `convert_rational(True, …)` is taken as 1). Pick one rule (likely
   `TypeError` for the wrong type, `BovnarArgumentError` for a bad value) and
   apply it.
6. **Recovered documents return quietly.** Under `continue_on_error`,
   `parse_with_options`, `parse_embedded_with_options`, the `stream::*_with_options`
   functions and their Python counterparts return what was recovered without
   an error; only `canonicalize` refuses a recovered read. The errors reach
   `on_error`, where there is one, and `recovery_count` on the reader. Decide
   whether the convenience functions should say so (an error, a flag, or a
   doc line).
7. **An exception or panic in `on_error` cannot stop a read by itself.** The C
   `on_error` callback returns nothing, so the binding parks the exception and
   the read stops at the next `on_verified`/`on_unverified`/`want_unit` call,
   or runs to the end with none installed, then re-raises (documented in
   doc/09 and doc/14). Stopping it at once would need a C change: a `bool`
   return, or a stop flag the reader checks.
8. **Rust `_with` means three things.** `Reader::read_mem_with` takes
   `ReadHandlers`, `Writer::to_mem_with` positional pretty/unit_flags/
   emit_version, `DocumentFeed::feed_with` handlers per call; `_with_options`
   takes an options struct. `DomDoc::parse_reader_capped_with_policy` sits next
   to `parse_reader_with_policy`. Renaming is source-breaking; settle it before
   the names are released in 2.1.0, or leave it.
9. **`canonicalize` with both sides failing.** Rust reports the output's error
   first (it is what stopped the read), Python the input's. Both failing in one
   call is near impossible; align if the code is touched again.

Added by the second review pass the same day (also design choices):

10. **A failing feed and the values before the error.** Rust's
    `ValueFeed::feed` appends the assignments completed before the error;
    Python's `DocumentFeed(values=True)` drops what the failing `feed()`
    completed (doc/09 §4.7). Each is documented; they disagree.
11. **Parity gaps nothing documents.** Rust `live::DocumentFeed` has
    `set_unit_policy`, Python's `DocumentFeed` no unit policy at all. Rust has
    `stream::parse_embedded_reader`; Python's `parse_embedded` takes bytes
    only. A mux stream's own key comes out of Rust's `ValueBuilder` as an
    empty `Bytes` value, not out of Python's `DemuxFeed(values=True)` (doc/14
    §4.9 says so now).
12. **float:16/32 subnormals print more digits than they need.**
    `bvn_format_double` takes the precision of a binary32/binary16 subnormal
    from the format's width (24 or 11 bits), not from the bits the subnormal
    has, so 2⁻¹⁴⁹ as `float:32` is `1.4012984643e-45`. It round-trips; the
    shortest form depends on how a reader rounds a `float:32` it reads as a
    double, which is the open question.

## Done — incremental streaming for Rust and Python (asked 2026-11-08)

**Status 2026-11-09:** every item has shipped on main (unreleased:
`bovnar::codec`, `bovnar::live`, the C callback sources and sinks, the push-fed
reader, frame reader and demux, `bvnr_write_flush`, the Python feeds and live
writers, the benchmarks, the docs, and on 2026-11-09 the full vector set of
item 5). Nothing is open. The paragraphs below describe the state before the
work, as the plan was written.

**Next release: 2.1.0** (decided 2026-11-09). It includes one
source-incompatible Rust change, taken in a minor version on the maintainer's
word: `ReadOptions` gained `max_text_bytes` and `max_array_items`, so a struct
literal naming every field no longer compiles (CHANGELOG, Changed). Mumcord
does not use `ReadOptions`.

The streaming layer of doc/10 (frames, octet multiplexing,
document-in-document, endless streams) is batch-only in both bindings.
`bovnar::stream` (`mux_dump`, `mux_load`, `load_documents`,
`embed_document`, their `*_to` and `*_reader` twins) and `bovnar.stream`
build the whole output in memory, or read their input to the end before
parsing it. So neither can serve a stream that never ends (a socket, a pipe,
a live feed), and every call pays its setup again. The C layer does not
close the gap on its own either:

- A pull callback cannot say "no data yet". An empty pull is end of input
  (`bvn_src_read_exact`, `bvn_lex_run`), so a short feed reads as a
  truncated frame or document. The fd backend avoids this only by waiting
  through `EAGAIN`.
- There is no public way to build a source or sink from
  `bvnr_pull_fn`/`bvnr_push_fn`, and the callbacks get no user pointer. The
  reader and writer copy the source or sink (`l->src = *src`), so a user
  pointer has to live inside the blob.
- The hot path allocates and copies. `bvnr_mux_send` mallocs and frees a
  64 KiB chunk on every message, the demux copies every message into a
  per-channel buffer, and `bvnr_doc_stream_read` mallocs every frame.

The goal is a generic, fast streaming API, split by job: the message framing
in pure Rust (it is small and has to cost nanoseconds), and live document
parsing plus callback I/O in C, which the bindings wrap. Shared vectors keep
the two implementations of the wire convention in step.

**Metadata before the stream** is a requirement on every layer (C, Rust and
Python): a stream document may start with ordinary text values, then open
the mux key, for example `.meta = { .codec = "av1"; .rate = <float:32,Hz>
30.0; }; .media = \x00 … \x00;`. Writers take that preamble, and readers
hand it over (raw bytes or parsed values) before the first message. Because
a NUL byte cannot occur in Bovnar text, the first `0x00` of a text-only
preamble is the mux opener, so the Rust codec can find the boundary without
a full parse and parse the preamble once per stream.

### 1. Rust: a pure-Rust framing codec in the crate

The octet chunk of spec §9 plus the two LEB128 varints of doc/10 §3, with no
FFI and no allocation per message. A prototype exists (Mumcord/mumcord PR
#682, `crates/bovnar-mux`, `#![forbid(unsafe_code)]`): byte-identical to
`bvnr_mux_send` on shared vectors, 9–34 ns per message to write and to peek,
0 allocations per message. Its numbers are the targets. Its code is not used:
Mumcord is AGPL-3.0 and Bovnar is MIT, so the codec is written from the spec
and the C reference.

- **`MuxWriter`**: the octet-stream opener, then per message the chunk
  header and the channel and length varints, written into a caller buffer
  next to the borrowed payload (ready for a vectored write), and finally the
  terminator. It splits messages over 64 KiB as `bvnr_mux_send` does, with
  the channel varint leading every chunk.
- **`MuxReader`**: fed bytes in arbitrary pieces, it returns
  `(channel, &[u8])`. The slice is borrowed when the message is contiguous in
  the input; otherwise the message is reassembled into a per-channel buffer
  that is reused. Unlike the prototype it accepts **every** valid stream:
  packed chunks (several messages in one chunk) and interleaved channels.
  The C demux's limits and errors stay: `max_message`, the 4096-channel
  limit, out of sync, and truncated at the end.
- **Peek / skip**: read the next message's channel, its length and its first
  payload bytes without touching the rest, then skip or forward it whole. This
  is for forwarders that route or filter by channel.
- **Frames**: the same incremental writer and reader for `BVF1` frames,
  handing each complete document's bytes to the caller (or to item 3's
  parser), with `max_document_size` as in `bvnr_doc_stream_read`.

### 2. C: callback I/O and live feeding

ABI additions only. The user pointer fits in the existing 64-byte
`bvnr_source_t`/`bvnr_sink_t` (the private layouts were 32 and 48 bytes on
LP64, and are 40 and 56 with the user pointer), so there is no layout change and no `BVNR_ABI_MAJOR` bump.

- **Callback source and sink**: constructors next to `bvnr_source_from_fd`
  and `_from_mem` that take a pull or push function, an optional flush and a
  user pointer, and a getter for that pointer inside the callback. A callback
  sink keeps `is_mem` false, so a failing socket reports
  `error_writing_to_sink` and the writer's buffer stays in use.
- **Push-fed document reader**: a reader that is fed bytes (`feed`) and keeps
  its lexer state across feeds, with an explicit `finish`. Truncation is a
  `finish` result, never a feed result. Events go through the existing
  callbacks.
- **Push-fed frame reader and demux** on top of it, with a pull-style "next
  message" or "next document" call as well as the callbacks, so a binding
  that pays for every callback (Python's ctypes) can drain a whole feed in
  one loop. A message that lies wholly inside the fed buffer is handed out
  from that buffer without a copy, valid until the next call.
- **`bvnr_mux_send` without an allocation per message**: the chunk buffer
  moves onto the writer or a mux session that outlives one send.
- Each addition gets C tests, a `python3 check_abi.py --update`, and the
  regeneration order under "Setup" (WASM, HTML docs, LLM views, `web/de/`).

### 3. Rust: live documents through C

- `std::io::Read`/`Write` sources and sinks for documents through item 2's
  callback source and sink, so the `*_reader`/`*_to` twins stream instead of
  going through memory.
- A feed-mode document parser over item 2's push-fed reader, one handle per
  stream and reused, for an endless document or for each frame from item 1.
- Embedded documents on a live stream: write one into an open writer; read
  one by feeding its octet chunks to a reused parser, not a new reader per
  call as `bvnr_parse_embedded` does.

### 4. Python: wrap the C feed API

- Feed objects for frames, demux and documents over item 2, drained with the
  pull-style calls so there is no ctypes callback per message. Messages come
  out as `bytes`, or as a `memoryview` valid until the next call.
- File-like objects and sockets as sources and sinks.
- Expectations stated in the docs: per-message cost in Python is in the
  microseconds (interpreter and ctypes), not nanoseconds. The benchmark shows
  the real figure.

### 5. Conformance

**Status 2026-11-08:** a minimal shared set ships first (`tests/stream_vectors/`:
a preamble plus mux stream, the same without preamble, a small frame stream,
three invalid vectors), checked by the C demux, the Rust codec and
`live::Demux`, the Python feeds, the C and Rust writers byte for byte, and the
CLI. The fuzz targets have shipped too: `codec` and `live` under `rust/fuzz/`,
and `bvnr_fuzz_feed` for the C feed readers. It covers
what the first consumer (Mumcord) relies on.

**Status 2026-11-09:** the full set below has shipped: 28 vectors (doc/10 §9),
read by every reader of the three implementations wherever cut and by `bovnar
mux list` / `frames list` (`tests/check_stream_vectors_cli.py`), written byte
for byte by every writer. "Input split at every byte" is the readers' cutting,
not a vector. Three vectors pin where the pure-Rust codec deliberately differs
from the C demultiplexer (`.codec_error` in the manifest).

Shared vectors across C, Rust and Python, each side reading the others'
output and `bovnar frames list` / `bovnar mux list` reading all of it:
input split at every byte, a 65536-byte chunk (length field 0), messages over
64 KiB, the terminator, packed chunks, interleaved channels, a length varint
split across chunks, 4096 channels and the 4097th, truncation, desync, and a
failed document among good ones.

### 6. Benchmarks

Per-message write, peek and full read for ~150 B, ~30 KB and ~400 KB
(multi-chunk) messages, release build, one core, next to the batch functions:
Rust codec, the C feed path, and Python. Targets for the Rust codec: ≤ 50 ns
to write and to peek, 0 allocations per message once warm. The figures go in
the docs.

### 7. Docs

doc/10 (layers and the new API), doc/14 (Rust), doc/09 and doc/10 §5
(Python), with one example each; the read/write API doc for the C additions;
a CHANGELOG entry.

### Order

1 first: it is self-contained and the prototype exists. Then 2, then 3 and 4
on top of it, with 5–7 alongside each step. The batch functions stay as they
are. Everything adds API and removes none, so this is a minor release with an
export-only ABI addition.

## 2.0.1 — released earlier the same day

`v2.0.1` is tagged on be88c9a. The tag's CI run published everything: the
GitHub release with seven assets (now including the universal
`bovnar-macos-2.0.1-801.tar.xz`), PyPI, and crates.io — the first crate
version CI published, through Trusted Publishing. The run needed five
attempts: GitHub had no macOS arm64 runner for the `macos` job ("not acquired
by Runner of type hosted"), so four were cancelled before anything was
published. A re-run of the failed jobs is the fix; nothing in the tree.

Zenodo 2.0.1, deposited with `zenodo_deposit.py`: source
10.5281/zenodo.23958544, documentation and specification
10.5281/zenodo.23958545 (in CITATION.cff and the README). `PRODUCTION_RECORDS`
now names the 2.0.0 records (it still named 1.1.0's).

**For the next release:** bump `version:` in CITATION.cff in the release
commit itself. v2.0.1 was tagged with it still at 2.0.0, which
`zenodo_deposit.py` refuses; the deposit ran with `--citation CITATION.cff`
(the working copy, version 2.0.1, same date) since the tag cannot move. The
deposit also refuses a working tree whose `doc/` differs from the tag, wiki
drafts included: deposit before editing `doc/`, or stash the edits.

The macOS archive was checked by hand: checksum, the same 83 files as the
Linux archive, every binary arm64 + x86_64 with minos 11.0, the arm64 slices'
ad-hoc signatures valid page by page, 341 exports equal to the ABI baseline.
It is not Developer ID signed or notarized: a browser download is quarantined
by Gatekeeper (`xattr -dr com.apple.quarantine`, or fetch with curl); the
README says so under the release archives.

## 2.0.0 — released the same day

`v2.0.0` is tagged on 796171a and pushed. The Rust crate `bovnar` 2.0.0 was
published to crates.io by hand from the tag with `rust/package.py`, and the
crate's Trusted Publishing is configured, so from 2.0.1 on a `vX.Y.Z` tag
publishes through CI's `publish_crate` job.

Zenodo has 2.0.0 on both lines, deposited with `zenodo_deposit.py`: source
10.5281/zenodo.23934686, documentation and specification (Markdown and PDF)
10.5281/zenodo.23934688; the concept DOIs resolve to them. A later release
runs the same script (sandbox first with `--make-test-base`, then `--host
zenodo`, review, `--publish`) and adds its DOIs to CITATION.cff and README.md.

The released tree is the one the third whole-project review left. What
the review changed is in CHANGELOG.md under 2.0.0, "Fixed — a third
whole-project review before release"; the Rust crate (PR #1) has its own
"Added — Rust bindings" entry, and the crates.io publishing work of the same
day (`rust/package.py`, the `rust_package`/`publish_crate` jobs) is in 8ad0130.
Verified on that tree: `./run_tests.sh` (full build, all 189
CTest tests and the fuzz sweep), the ASan+UBSan build 163/163 (`-LE python`),
`BVNR_WERROR` builds clean with GCC 15.2 and clang 21, and the Rust crate's
`cargo test --locked` 1373/1373 (with `test_writers_agree -- --ignored`),
`cargo clippy -- -D warnings`, `cargo fmt --check` and the 1.88 MSRV. The
WASM module, `web/`, the LLM views, `web/de/` and the IETF draft renderings
were regenerated from the sources.

The CI compiler is now GCC 15.2 (`ubuntu-latest` moved to Ubuntu 26), not 13.
A Rust toolchain (rustup, stable + 1.88) was installed user-locally in
`~/.rustup`/`~/.cargo` for this review, without touching the shell profile;
`rust/target/` is git-ignored.

## Closed 2026-11-04 — with 2.0.1

1. **The Downloads menu and the macOS archive.** The menu links the seven
   v2.0.1 assets, macOS among them, and `check_release_links.py` expects
   `bovnar-macos-<version>-<count>.tar.xz`. `zenodo_deposit.py` finds the
   source tarball by name, not by its place in that list.

## Closed 2026-11-04 — after the release

1. **ABI baseline.** `tests/abi/bvnr-2.abi` and `.syms`, checked by
   `bvnr_abi_baseline` (`check_abi.py`). A change to a public header or the
   exports fails it: `python3 check_abi.py --update` for an addition, a
   `BVNR_ABI_MAJOR` bump for a break (CONTRIBUTING.md).
2. **Long binary128 literals** — every length is correctly rounded: when the
   digit budget dropped a tail, the result is checked against the midpoints'
   exact decimal expansions (`bvnf_float_settle_decimal`,
   `bvnf_ieee_settle_decimal`). `test_bvnf_midpoint_past_budget`.
3. **Hex text near the precision ceiling** — read bit for bit
   (`bvnf_from_hex`), exact at any precision up to a binary exponent of ±1e9.
   `test_bvnf_hex_any_exponent`.
4. **An annotation before an octet stream** — decided on the maintainer's
   word: the writer keeps accepting it, as the reader does (spec §6.1:
   carried, not checked; producers SHOULD NOT write one). A refusal would make
   `bovnar pretty-print`, `convert` and the DOM writer fail on a document
   `validate` accepts, since they pass the annotation through.
5. **wasm32-wasip2 tests** — `cargo test --target wasm32-wasip2 --lib --tests`
   runs under wasmtime in the `rust_wasm` CI job (1315 passed locally,
   wasmtime 50.0.1, wasi-sdk 34). Integration tests write their temp files to
   `CARGO_TARGET_TMPDIR` (WASI has no `temp_dir()` or pid); the thread test
   and the callback-panic test are ignored there; doctests stay native.
6. **Long decimal literals at a small magnitude** — the kept digits no
   longer crowd out the denominator: for 0.D * 10^x with x < 0 the digit
   budget leaves 10^(kept - x) its room, down to sig_bits + 64, and the
   midpoint check settles the rest. A 5000-digit literal at precision 113
   reads down to ~1e-9810 (was ~1e-6165).

## Closed 2026-11-04 — what the third review left to the maintainer

1. **`MBtu`, `Mlb`** — done on the maintainer's word: `MBtu`, `MBTU`, `Mbtu`,
   `Mlb` and `Mlbs` are `.compact_exceptions` (`error_unit_illegal`); write
   `k~Btu`/`k~lb` or `M~Btu`/`M~lb`.
2. **crates.io 2.0.0** — published by hand from the `v2.0.0` tag with
   `rust/package.py` (Trusted Publishing cannot create a crate); Trusted
   Publishing is configured, and `publish_crate` takes over from 2.0.1.

## Closed 2026-11-03 — what the second review left to the maintainer

All four done on the maintainer's word ("do all"):

1. `pretty-print --canonical` (`BVN_NUM_CANONICAL`) writes every annotation in
   one form: width written out, default `_10` and `no_unit` left out.
   `test_canonical_annotation_form` in `bvnr_writer_test`.
2. `unece:A8`, `J55`, `P14`, `M77`, `F90`, `C55` spell their definitions out;
   `PS` stays the psi. Pinned in `bvnr_unece_test`.
3. `bovnar convert` json -> bvnr writes integers wider than 64 bits as
   `<uint:N>`/`<sint:N>` (refused only inside an array).
   `bvnr_cli_convert_wide_int`.
4. The IETF draft's front matter carries `date: 2026-11-04`; move it with the
   release date.

## Setup — picking it up on a new machine

- `build/` and `build-asan/` are git-ignored and do not travel. Configure
  them again: `cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo .` and
  `cmake -B build-asan -DBVNR_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug .`, then
  `./run_tests.sh` (it builds first; `ctest` alone does not). Note that no
  CTest test may carry PASS_REGULAR_EXPRESSION (it replaces the exit status);
  a test that checks output goes through `cmake/cli_expect.cmake`.
- `web/de/` is git-ignored as well; `python3 gen_i18n.py de` recreates it.
- A change to a public header or to the library's exports fails
  `bvnr_abi_baseline` until `python3 check_abi.py --update` rewrites
  `tests/abi/` (with `build/libbvnr.so` built, for the export list).
- After any C change, regenerate in this order: `wasm/build_wasm.sh` (needs
  emsdk), `gen_html_docs.py`, `gen_llms.py`, `gen_csp.py` if an inline script
  in `web/index.html` changed, `gen_i18n.py de` last. The WASM freshness
  stamp now also covers `wasm/build_wasm.sh`, and the sync gate now compares
  every file of `dist/wasm` with its source, so an edit to `wasm/README.md`,
  `wasm/package.json`, `LICENSE` or `THIRD_PARTY_NOTICES.md` also needs a
  `build_wasm.sh` run.
- The IETF draft renderings are rebuilt by hand from
  `doc/ietf/draft-sonntag-bovnar-00.md` with `kramdown-rfc` and
  `xml2rfc --text --html`; `check_ietf_draft.py` checks all four.
- `check_profile_factors.py` needs the publishers' files: run it with
  `--fetch` once (network) before `--strict`. The review's run against them
  compared 10 917 rows with no mismatch.
- The CI tests job now configures with `-DBVNR_WERROR=ON`. The first push
  runs it on GitHub for real; if a runner's GCC warns where 13 and 15 did
  not, that job is where it shows.

## Closed 2026-11-03 — the decisions the first whole-project review left

1. **The 2.0.0 date.** Set to 2026-11-04 on 2026-11-03 in CHANGELOG,
   CITATION.cff (`date-released`), the JOSS paper and both wiki drafts (was
   2026-10-10). If the tag lands on another day, move them again.
2. **Native `eq`** — decided 2026-11-03: it stays an alias of `val`, the
   water-analysis equivalent (½ mol), so native `meq/L` is half of
   `ucum:meq/L`. Stated as settled in doc/05, with the clinical trap in doc/07,
   the profile note in doc/11 and the FAQ; pinned in `bovnar_ucum_test`.
3. **`ucum:[IU]` and `ucum:[iU]`** — done 2026-11-03: one opaque id, `[iU]`
   an alias of `[IU]` through `.opaque_aliases` in `ucum.bvnr` (doc/11 §3.6,
   `test_arbitrary` in `bovnar_ucum_test`). UCUM's block is 200000–200039.
4. **Quoted float literals take a leading `+`** — decided 2026-11-03: kept.
   doc/03 §6.3 ("Signs in Quoted String Literals") and the IETF draft say a
   quoted float may begin with one `+` and a quoted integer may not.
5. **`-2026-06-15` under `#!bovnar 1.1`** — decided 2026-11-03: the reader's
   `error_invalid_datetime_literal` where the value ends stays; doc/03 §5.1
   and the IETF draft now say a leading `-` keeps the skeleton.
6. **Profile coverage** — done 2026-11-03: of the 53, eleven map, 42 stay
   refused with a reason each in `REVIEWED_STALE` (check_profile_factors.py),
   and `qudt-qk:CostPerEnergy`/`CostPerPower` are refused (they dropped the
   currency). The sweep lists 0. `qudt:LA`, which followed QUDT's apostilb
   multiplier against its own label and ucumCode (Lambert, `Lmb`), is refused
   like `unece:P30`.

## Closed 2026-11-03 — the open items of the 2026-10-12 spec review

1. **`[]` was formally ambiguous in the grammar.** `row-content` now needs a
   `filled-elem` when the row has one element, so `[]` derives only as the
   empty row (`present-value` split out of `raw-value` in `doc/12_bovnar.ebnf`;
   both ABNF copies in the IETF draft; one sentence in spec §7.2 and the
   draft). No change to what the reader accepts: `[]`/`[ ]` are empty,
   `[<uint:8>]` is one typed null.
2. **`bvnr_reader_get_error` after a recovered error.** New spec §13.4, plus
   the read/write API doc, the Python error-state properties and the header: the getters describe the error that
   ended the read; after a read that returned true they give `error_none` and 0.
   Tested in `bvnr_high_severity_test` (`test_error_getters_after_recovery`)
   and asserted on every input by the reader fuzz harness.
3. **Appendix C** lists the five length limits, their 65 535 ceiling and the
   unit group-nesting limit.
4. **FAQ datetime passage** (`doc/02_bovnar_faq.md`): the error split now
   matches the spec (a wrong-width field is `error_invalid_datetime_literal`),
   and injectivity is limited to the leap seconds the table records.

Review coverage: spec §12–15 and §16–18 were read in full against the code,
and the whole spec was read for cross-section contradictions (two reviewers,
every claim reproduced). The verified wording findings are fixed in doc/03,
doc/12 note n) and the IETF draft.

## Closed 2026-11-03 — the implementation leads from that review

All five fixed, each with a regression test (`test_recovery_review_2026_11` in
`bvnr_high_severity_test`, `test_writer_review_2026_11` in `bvnr_writer_test`)
and a CHANGELOG entry under 2.0.0:

1. A stray multi-byte character is one error, not one per byte.
2. `strict_version` ends the read under `continue_on_error` (an unsupported
   version, and under `strict_version` a malformed directive).
3. `bvnr_reader_get_skipped_bytes` counts from the offending byte up to, not
   including, the resume boundary.
4. `bvnr_write_bvnf` checks the width before writing the key.
5. The writer's key cap is 65 535 bytes, the reader field's ceiling, not 255.

Nothing is open.

## How the review was run

Two read-only subagents, one on §12–15, one on §16–18 plus a whole-spec
consistency pass, each reproducing its claims with the CLI or small C probes
against `build/libbvnr.so`; every finding was re-checked before it was fixed.
