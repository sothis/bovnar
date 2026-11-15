# TODO

## State 2026-11-15 — 2.2.0 published

v2.2.0 is tag `610809d`. The GitHub release is immutable, so the tag could
not move when `publish_crate` failed. The failure was `package.py` refusing
a publish without `--prebuilt`: `7cc7687` gave those archives to
`rust_package` and not to `publish_crate`, and v2.2.0 was the first tag that
ran that path. PyPI already had 2.2.0 and the GitHub release was complete.
The workflow fix is `ce320ad` on main and `bb28b13` on `2.2.x`. The crate
was published from `610809d` (the tag), with that run's own prebuilt
archives, by a one-shot `workflow_dispatch` of `build-and-package.yml` on
`ci/publish-crate-2.2.0`. That branch was deleted once crates.io listed
2.2.0.

The downloads menu, the JSON-LD `softwareVersion` and
`web/download/index.html` name 2.2.0 at build count 996 (`275f03a` on main,
cherry-picked to `2.2.x` as `7299623`). The sizes are the archives' own.
The live site is published to `json@bovnar.io:/var/www/html`. Zenodo has the
source as 10.5281/zenodo.24250459 and the documentation and specification as
10.5281/zenodo.24250462 (CITATION.cff and the README). `PRODUCTION_RECORDS`
names those records. The deposit fetches the release assets from GitHub:
`32bad0c` had pointed `DOWNLOAD_PREFIX` at the site path `/download/`, which
is not a URL the deposit can fetch. `orcid_prefer.py` found both works on
the ORCID record and could not make them preferred: the ORCID session cookie
is expired. The wiki drafts in `doc/wiki`
carry a 2.2.0 history paragraph; there is still no GitHub wiki to publish
them to.

`BVNR_VERSION_SUFFIX` is `"-dev"` again on main, and `[Unreleased]` is open
and empty. `2.2.x` stays at 2.2.0.

## State 2026-11-15 — Wine's tmpfile(), not the writer

The Wine trap from the entry below is closed, and it was never a library bug.
`push_fd`'s counter is exact: across every trapped run the fd position equalled
`bvnr_writer_bytes_written()` exactly, and the serializer's flush buffer held a
complete, valid document (`flush: wbuf_pos=19 buf=[.k0 = <uint:32> 0;\n]`) while
the read came back as a *different* document -- a different `pretty` flag and
different content. The bytes between the fd and the read were wrong; the writer
was right. `src/` is unchanged, and the instrumentation used to establish this was
reverted.

Wine's `tmpfile()` is the culprit, and it needs concurrency: native and serial
Wine are both clean (200k iterations, and 24 parallel native runs), while two or
more concurrent processes trap reliably. Probed directly, 8 concurrent processes
each calling `tmpfile()` in a loop had 7 of 8 fail outright (`tmpfile fail i=29`,
`i=396`, `i=55`, `i=0`, ...), and the file that does come back is not reliably
private. That also explains why `rewind()` -- the fix in the entry below, correct
for the serial case -- could not cover it: there is nothing to rewind when
`tmpfile()` fails or hands back shared storage.

`harness_fd_stream` now uses one `mkstemp()` file per process instead of
`tmpfile()`, created once and reused: `mkstemp` gives a name private to this
process, so there is no collision. The file is truncated (`ftruncate`) and
rewound before the sink exists, which is what makes reuse safe. It is created
once rather than per iteration because `unlink()` fails on Wine (it returns -1
and the directory entry stays), so a per-iteration `mkstemp` leaks an entry every
time -- 800k entries at 200k x 4, which is what made an earlier attempt look like
a hang. The cost is one leaked entry per process; where `unlink()` works the file
has no name at all.

Verified after the change: native `ctest` 200/200, ASan+UBSan 14/14 on the writer
and fuzz suites, the Wine cross tree 165/165 serially, and 200k iterations x 8
concurrent processes x 4 rounds with 0 failures -- the configuration that
previously trapped 4 of 4. Native and Wine serial 200k both pass. One caveat: the
Wine scratch file leaks one `/tmp` entry per process, unavoidable where
`unlink()` fails; 32 processes is 32 files.

Two incidental fixes went in with it. `tests/.clangd` is new: clangd resolves a
relative `-I` against the config file's own directory, so the root `.clangd`'s
`-Iinclude` meant `tests/include` for files under `tests/`, which is why the
editor reported `include/bovnar.h` (91 KB, present) as not found. And the stray
indentation the previous session's diff left on a closing brace in
`bovnar_fuzz_writer.c` is corrected.

## State 2026-11-15 — 2.2.0, cut but not tagged

Every version field reads 2.2.0 with `BVNR_VERSION_SUFFIX` back to `""`, and
`[Unreleased]` became `[2.2.0] - 2026-11-15` with its link ref. `RELEASE_NOTES_v2.2.0.md`
is the summary the release job will attach.

**Branching, from this release on:** 2.2.0 is cut on `2.2.x`, a maintenance
branch created from `main` before the release commit and pushed, so the tree the
tag will name runs CI as a branch before the tag exists. `main` fast-forwards to
it once the tag is published, and patch releases (2.2.1 and up) are cherry-picks
onto that branch — only fixes that are on `main` and also affect this line.
CONTRIBUTING's Releases section is the procedure; `main` stays where development
happens.

The minor is deliberate and the reasoning is written down because the obvious
precedent does not support it. The CHANGELOG's rule is that the implementation's
major moves when a *program* built against it has to change; the additions here
are callable API (`DocumentFeed.set_unit_policy`) and packaging (prebuilt C
libraries for MSVC and both macOS targets), so nothing breaks and the ABI does
not move. 2.0.1 and 2.0.2 shipped `### Added` sections as patches, but those
sections added *no* API at all -- an ABI baseline file, a wasm test runner, a
macOS archive, a provenance manifest. 2.1.0 is the last release that added
callable API, and it took a minor. `DocumentFeed` exists at the `v2.1.0` tag and
`python/bovnar/stream.py` there has zero occurrences of `set_unit_policy` (it
landed in ea725d9, contained by no release tag), so this is genuinely new public
Python API. Diffing the `BVN_API` symbol list against `v2.1.0` confirms the C ABI
is unchanged.

The full sweep ran on this tree: `./run_tests.sh` 200/200 with the fuzz sweep at
5000, ASan+UBSan 174/174, `cargo fmt --check`, `clippy -D warnings`, `cargo test
--locked` 1584/1584, the wasm32-wasip2 leg 1479/1479, `cargo check` on the fuzz
targets, the Wine cross tree 165/165 serially, the wheel built and smoke-tested
(import, `version()` == 2.2.0, a round-trip), and 3574 Python tests. The WASM
module, `web/docs`, `llms-full.txt` and `web/de` are regenerated, and the CLI
reports 2.2.0.

**Not done, deliberately:** the release commit. The tree is staged and swept but
uncommitted, because more may be added or changed first. What remains for the
commit itself: `RELEASE_NOTES_v2.2.0.md` is written, `gen_downloads.py` needs the
released artifacts under `build/releases/2.2.0/` (fetched after the tag), and the
Downloads menu in `web/index.html` is hand-maintained and still names 2.1.0 --
correct until the archives exist.

## State 2026-11-10 — 2.1.0 released

The release commit moves every version field to 2.1.0: CMakeLists.txt,
`bovnar.h` (with `BVNR_VERSION_SUFFIX` back to `""`), pyproject.toml and its
cibuildwheel test command, `python/bovnar/__init__.py` and its version test,
the Rust crate and both lockfiles, CITATION.cff (`version` 2.1.0,
`date-released` 2026-11-10), SECURITY.md's table, rust/README.md, the version
lines in doc/02, doc/03, doc/09, doc/11 and doc/14, the IETF draft's date
(with its three renderings), the wiki infoboxes and the highlighter. The
WASM module, `web/docs`, the LLM views and `web/de` are regenerated, and
RELEASE_NOTES_v2.1.0.md is the summary the release job attaches.

The fifth review pass (2026-11-10) found and fixed: a demux channel committed
before its hash insertion could fail; a DOM policy conversion of a rational
far below binary64's range failing instead of storing signed zero; a NULL
event replacing the writer's latched first error; Python's `float_fix`
`stored_value` rounding to the decimal context instead of being exact; fd
arguments accepting bools and out-of-`int` values (reader, writer and DOM);
`write_bool` truth-coercing a non-bool; `Writer.set_unit_policy` escaping the
callback re-entrancy guard; and a refused `memoryview` call not retrying the
release once the buffer made from the view was gone. The pull reader's
callbacks are documented as not re-entrant, as the fed reader's already were.
The Rust codec and live modules and the C feed/frame/demux paths were read
again and no defect survived verification.

2.1.0 is tagged on 4895049 and published: the GitHub release carries the
eight archives including `bovnar-freebsd-2.1.0-944.tar.xz`, PyPI and crates.io
have 2.1.0, and Zenodo has the source as 10.5281/zenodo.24111262 and the
documentation and specification as 10.5281/zenodo.24111263 (CITATION.cff and
README name them). The Downloads menu points at the release (eight links,
build count 944), `check_release_links.py` passes, and the site and its PDFs
are published. The wiki article text is in doc/wiki; there is no GitHub wiki to
publish it to (the repository has none), so the article stays in the tree and
that step is closed as not applicable. `-dev` is restored and the [Unreleased]
CHANGELOG section is the next cycle's summary.

## State 2026-11-14 — x86-64-v2 floor, and what the Wine trap was

The instruction set is now pinned rather than inherited. `CMakeLists.txt` adds
`-march=x86-64-v2` to every x86_64 translation unit (library, CLI, tests, fuzz
harnesses) and says so at configure time; the Wine cross tree inherits it, so
local, CI and cross build the same ISA. MSVC keeps its own x64 baseline: it has
no /arch: for v2 and the next step up is /arch:AVX (v3), a much wider jump than
the one asked for -- the note in CMakeLists.txt is the record of that.
`rust/build.rs` adds the same flag for x86_64 targets when the compiler takes it,
so the crate's C half matches upstream.

Verified after the change: native `ctest` 200/200, ASan+UBSan 174/174, the cross
tree builds and runs.

The Wine trap turned out to be two things.

**Fixed:** `harness_fd_stream` trusted `tmpfile()` to hand back an empty file at
position 0. On Wine it does not -- it returns a file with a previous iteration's
bytes in it and the fd positioned after them, so the fd sink wrote the document
at a nonzero offset while the read below started at 0. `rewind(f)` before the
sink exists fixes that, and the comment says why.

**Correction, 2026-11-15 -- that diagnosis was wrong.** The byte count was never
short. `push_fd` is exact: in every trapped run the fd position equalled
`bvnr_writer_bytes_written()` exactly, and the serializer's flush buffer held a
complete, valid document. The library is blameless and `src/` was not touched.
See "Wine's tmpfile(), not the writer" at the top of this file for what was
actually happening.

## State 2026-11-14 — main swept, ready to tag 2.1.1

The full sweep ran on this tree (ef0b543 plus the four loose ends below), not
just CI: `./run_tests.sh` 200/200 with the fuzz sweep at 5000 iterations
(reader, dom, utils, feed, and the writer smoke); the ASan+UBSan build 174/174
(`-LE python`, `build/asan`, one skip: `bvnr_alloc_test` needs glibc); `cargo
fmt --check`, `cargo clippy --locked --all-targets -- -D warnings`, `cargo test
--locked` all green, `cargo check` on the fuzz targets,
`test_writers_agree -- --ignored` against the built CLI; the wheel built and
smoke-tested out of the tree (import, `version()`, a round-trip, `DocumentFeed`
and `set_unit_policy`); the CLI reports `2.1.0-dev` and the WASM module `2.1.0`
/ spec 1.2. The two CI legs that were never run locally — wasm32-wasip2 and the
Wine cross tree — ran afterwards, below.

The wasm32-wasip2 sweep and the Wine cross tree ran on 2026-11-14, the two CI
legs that had never been run locally.

**wasm32-wasip2** — wasmtime 50.0.1 and wasi-sdk 34.0 were installed under
`~/.local` (a tarball unpack, no root needed: both are static binaries). Then,
as the `rust_wasm` job does: `cargo build --locked --target wasm32-wasip2` and
`cargo test --locked --target wasm32-wasip2 --lib --tests` with
`CARGO_TARGET_WASM32_WASIP2_RUNNER="wasmtime run --dir <repo>"` and
`WASI_SDK_PATH=$HOME/.local/wasi-sdk-34.0-x86_64-linux`. **1479 passed, 0
failed.** The toolchain is `~/.cargo/bin/cargo` (rustup, stable 1.99.1, which
already has the `wasm32-wasip2` std) — `/usr/bin/cargo` is the distro 1.93.1
and cannot target it.

**Wine cross tree** — `cmake -B build/mingw
-DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64-x86_64.cmake
-DCMAKE_BUILD_TYPE=Release -DBVNR_BUILD_TESTS=ON .`, then `ctest --test-dir
build/mingw`. Wine 10.0 is installed, and the toolchain file picks it up as
`CMAKE_CROSSCOMPILING_EMULATOR`, so the cross-built `.exe` tests run. 168 tests
registered, 3 registered-and-DISABLED (the cross tree's WASM/doc gates, which
exec the built CLI bare), `bvnr_alloc_test` skipped.

Serial (`-j1`) it is **163 of 164**, and the one failure is not a failure:
`bvnr_cli_regressions` hits its native-calibrated `TIMEOUT 120` while Wine runs
its 105 CLI cases. Measured: 6 s with `TMP_DIR=/tmp`, 175 s with `TMP_DIR` on
this repository's filesystem — the same 105 cases pass either way, so the cost is
Wine's file I/O on that mount, not the CLI. CTest's `--timeout` does not
override a per-test `TIMEOUT` property, so the fix is either a cross-aware
TIMEOUT at that one site or a scratch dir on tmpfs.

In parallel (`-j4`, `-j8`) three tests trap intermittently:
`bvnr_fuzz_writer`, `bvnr_fuzz_writer_deep`, `bvnr_fuzz_writer_thorough`, all at
the same place — `readback_validate` refuses the writer's own output, with
`bvnr_write_finish` true, `bvnr_writer_get_error` `error_none`, and a document
truncated right after its type annotation (`bw=15`, `.k0 = <uint:32>` for a
one-byte input). It is Wine-only and does not reproduce in isolation: the same
input alone passes 20/20, the whole suite passes 20000 iterations serially, and
the ASan build passes 200000 iterations natively. The failing iteration differs
run to run, so it depends on run history or heap layout rather than on the
input. The sink-failure paths that are easy to construct (a callback sink that
refuses one push and recovers, one that refuses all of them) report correctly:
the failure is latched and `bvnr_write_finish` returns false. What the source
does show is that `bvn_ser_push`'s failures reach `bvnr_writer_get_error` only
where a caller latches them — the emit paths `return false` without one — so a
failure in a separator-sized emit can still leave `last_error` at `error_none`.
That is the next thing to chase, and it wants a debug cross build under Wine to
see the writer's state at the trap; it does not gate the tag, which is green
natively, under ASan, and on wasm32-wasip2.

Reproduce the two sweeps with:
    # wasm32-wasip2
    export PATH="$HOME/.cargo/bin:$HOME/.local/bin:$PATH"
    export WASI_SDK_PATH="$HOME/.local/wasi-sdk-34.0-x86_64-linux"
    export CARGO_TARGET_WASM32_WASIP2_RUNNER="wasmtime run --dir $PWD"
    cd rust && cargo test --locked --target wasm32-wasip2 --lib --tests

    # Wine cross tree (serial: Wine under -j4 and up is where the writer traps)
    cmake -B build/mingw -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64-x86_64.cmake \
          -DCMAKE_BUILD_TYPE=Release -DBVNR_BUILD_TESTS=ON .
    cmake --build build/mingw -j"$(nproc)"
    WINEDEBUG=-all ctest --test-dir build/mingw -j1


Four loose ends closed on the way: `reader.py`'s `want_unit` callback had an
unreachable `BovnarArgumentError` guard after `_as_vu` became the one door for a
unit-shaped argument (it now surfaces `_as_vu`'s `TypeError`, which names the
type, with a test pinning both halves — a `str` refused, a `Unit` accepted);
`package.py`'s provenance literal had two keys on one line; the workflow's
80-column rewrap got a note saying why the two URL continuations must sit at the
block indent (YAML strips the block's own indent, so one space more puts a space
inside the URL); and this file's "CI is green on 7f51b2d" line was two builds
stale.

What is left is the release commit: the version fields (2.1.0 -> 2.1.1,
`BVNR_VERSION_SUFFIX` back to `""`), `RELEASE_NOTES_v2.1.1.md`, and the
regeneration that follows a version bump. The CHANGELOG heading is already
written; `[Unreleased]` becomes 2.1.1. The `### Added` section is a new public
Python method (`DocumentFeed.set_unit_policy`) — a minor by the rule at the top
of the CHANGELOG, and 2.0.1/2.0.2 both shipped an `Added` section, so 2.1.1 is
consistent with the project's own precedent. No C symbol was added, so the ABI
does not move.

## State 2026-11-13 — main after 2.1.0

The tag is where the last full sweep was run. Since it, main carries the work
of the next cycle, all of it under [Unreleased] and all of it on CI: the Rust
crate shipping prebuilt C libraries for `x86_64-pc-windows-msvc`,
`x86_64-apple-darwin` and `aarch64-apple-darwin` (`c/prebuilt/`, `PROVENANCE.json`,
a `rust_prebuilt` CI job), the Python binding-review follow-ups (`_as_vu` as
the one door for a unit-shaped argument, `DocumentFeed.set_unit_policy`,
`canonicalize` reporting the sink's failure first, `FrameFeed`'s callbacks built
outside the constructor, `DomNode.as_int_str()` reading bytes), and the tooling
around them: the runner-shortage retry in CI, the rewrapped workflow, the
link-search separators, the `check_*.py` gates through one helper, the clangd
include paths, the Downloads pages and the ORCID preference steps.

The last of the binding-review leftovers closed on 2026-11-13: item 6 as a doc
line in doc/09 §11.6 (`loads`/`load` build and drop their reader, so the count
is reachable only through `Reader` and the feeds), and item 12 as code -- a
`float:32`/`float:16` subnormal now prints the digits it has, `2**-149` under
`float:32` being `1.4e-45` rather than `1.4012985e-45` (CHANGELOG Fixed, doc/08
and the `format_double` rows). Items 4 and 7 are closed as 3.0 work with the
reason recorded. `.clangd` gained `build/generated` and `-x c`, without which
the editor's clang could not parse the unit tables and read the C headers as
Objective-C++.

Version fields read 2.1.0 with `BVNR_VERSION_SUFFIX` back to `"-dev"`; the suffix
reaches only the `bovnar version` banner, so no other field moves until the next
release. CI is green on ef0b543: all 22 jobs of Build & Package (including
`rust_prebuilt`), and CodeQL.

What CI does NOT run is the local sweep: `./run_tests.sh` (the fuzz sweep at an
iteration count you choose — CI runs only the pinned registered fuzz tests and
`cargo check` on the fuzz targets) and the Wine-driven cross tree. The sweep the
2.1.0 entry records — `./run_tests.sh`, the ASan build, `cargo test`, clippy,
fmt — was run at the tag, not on this work. clippy and fmt HAVE run on this work
in CI (`Rust (Linux x64)`), which is worth knowing before re-running them by
hand: they are green on ef0b543.

Verified on this tree for the subnormal change: `ctest` 200/200 after
regenerating the WASM module, `web/docs`, `web/llms-full.txt` and `web/de`
(the C change moved the amalgamation and the docs), `cargo test --locked`
1584/1584, the ASan build's utils/writer/dom suites, the utils fuzz harness at
5000 iterations, and the reader round-trip of every emitted literal (`1.4e-45`
under `float:32` reads back as exactly 2^-149, checked with exact rationals).

ORCID: DataCite auto-update put both 2.1.0 works on the record, but initially
the source-code work was not preferred (2.0.2 was, display-index 6 against 1).
ORCID's API cannot fix that: only the client that created a work (DataCite)
may update it, and the record owner's selection is a web request.
`orcid_prefer.py --tag vX.Y.Z` makes the release's two works preferred and
featured using a signed-in orcid.org session cookie in
`~/.config/bovnar-orcid.cookie`; `--check` verifies (featured needs the
cookie). Future deposits do it in the same command with `zenodo_deposit.py
--publish --orcid-prefer` (or run `orcid_prefer.py` after DataCite catches
up). ORCID's session cookie is named `SESSION` (with
`XSRF-TOKEN`/`AUTH-XSRF-TOKEN` beside it), not `JSESSIONID`. Both 2.1.0 works
are preferred now (source display-index 9, documentation 8) and featured at
positions 1 and 2 (set 2026-11-10).

Verified on this tree: `./run_tests.sh` 200/200 with the fuzz sweep, the
ASan+UBSan build 174/174 (`-LE python`), `cargo test --locked` 1584/1584 and
the same under 1.88, `cargo clippy --locked --all-targets -- -D warnings`,
`cargo fmt --check`, `cargo check` on the fuzz targets,
`test_writers_agree -- --ignored`, the wheel (built and smoke-tested), and
the CLI and WASM module both report 2.1.0 / 20100. The wasm32-wasip2 tests
run in CI's `rust_wasm` job; wasmtime is not installed here.

## State 2026-11-09 — main was ahead of 2.0.2

main carried the unreleased 2.1.0 work (CHANGELOG, Unreleased): the streaming
layer for both bindings and the binding plumbing, with three review passes
fixed (the last on 2026-11-09, CHANGELOG Fixed, "second pass"). The tree then
still said 2.0.2 (`-dev` in the CLI), and the web pages on main already
described the 2.1.0 API: the site is published after the tag, not before.

What the 2.1.0 release commit had to move (all done on 2026-11-10; nothing
gated the doc lines):
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

1. **Feed callbacks differ between the bindings.** Rust's `FrameParser`
   (`feed_with`) and `live::Demux` (`next_with`/`finish_with`) take
   `ReadHandlers` (`on_error`, `want_unit`, `on_unverified`) on each call.
   Python's `FrameFeed` and `DemuxFeed` take only the `READ_OPTIONS`, as do
   `load_documents`, `mux_load`, `parse_embedded`, `loads`/`load` and
   `canonicalize`; only `Reader.read_*` and `DocumentFeed` take the callbacks.
   doc/09 §4.7 now states this (the option taken on 2026-11-10), so the
   remaining choice is whether to add them to the Python feeds later. The other
   way round, Python's `FrameFeed(return_errors=True)` has a Rust equivalent
   only in batch form (`stream::load_documents_results`).

    **Decided:** Python's feeds keep the options-only model; nothing changed.
        `FrameFeed` and `DemuxFeed` hand out whole messages and documents, not
        values, and an error of their own reaches the caller as the exception of
        the `feed()` it happened in, which is the same report `on_error` would be.
        Rust's per-call `ReadHandlers` stay. doc/09 §4.7 now says so where a
        reader meets the table, so the difference is stated rather than implied.

2. **Writer limits are not bound.** `WriteOptions` (Rust) and the `Writer.to_*`
   keywords (Python) expose the nesting caps and `on_event`, but not the five
   length caps, `max_array_items`, `max_text_bytes`, `max_file_size`,
   `continue_on_error` or `on_error` of `bvnr_write_flags_t`. The read side now
   binds every flag. `dumps`/`dump` expose neither `on_event` nor the nesting
   caps.

    **Decided:** not bound, on the C header's own word. `include/bovnar.h`
        says the writer consults only the two nesting caps, `on_event`, `userdata`,
        `unit_flags` and `emit_version`; the length and count caps,
        `continue_on_error` and `on_error` "are accepted for struct symmetry but
        are NOT consulted on the write path". Binding them would expose knobs that
        do nothing. The two that are consulted are on `Writer` / `WriteOptions`
        (doc/09 §12.1, doc/14 §11.1); `dumps`/`dump`/`to_string` stay the
        no-options path, and their docs say where to go for one.

3. **Size-cap defaults and meanings disagree.** `Reader.read_file` defaults to
   `max_file_size=16 MiB` (`MAX_FILESIZE_BYTES`); `read_mem`, `read_fd`,
   `read_stream`, `loads`, `load` and the stream functions default to 0, which
   is no cap. `DomDoc.parse_fd`/`parse_file(max_bytes=0)` and Rust's
   `parse_reader_capped(0)` mean the 256 MiB DOM cap, not unlimited.
   `max_file_size` caps one document in `load_documents`/`FrameFeed` but the
   whole stream in `mux_load`/`DemuxFeed` (documented). One meaning for 0, and
   one default, would be easier to explain; either is a behaviour change.

    **Decided:** one rule, stated in doc/09 §11.2 with the options table: `0`
        is that entry point's own documented default, never "the C field's
        default", and never "unlimited" unless the row says so. The entry points
        that bound a whole input keep their cap — `read_file` at 16 MiB,
        `DomDoc.parse_fd` at 256 MiB with no unlimited mode at all — while the ones
        handed a buffer already in memory leave `max_file_size` unlimited, because
        a cap there buys nothing. The defaults stand: loosening a cap is the one
        direction that costs a caller memory a document could have taken.

4. **A refused DOM builder node.** When `DomDoc::add` refuses a key (a
   duplicate, a non-identifier), Rust destroys the node; Python leaves it with
   the caller, who may add it elsewhere. Rust's `add` returns `()`, Python's the
   new `DomNode`; Rust's duplicate-key error carries `DuplicateStructKey`,
   Python's is a plain `BovnarArgumentError`. Align on one model.

    **Decided:** each binding keeps its model and both are now documented, so
        the choice is visible at the point of use: Rust's `add` takes the node by
        value and hands it over (a refused key is `Error::Argument` and the node
        goes with it, doc/14 §12.5), Python's leaves a refused node with the caller,
        who may add it elsewhere (doc/09 §13). Aligning on Rust's would drop a real
        Python feature; aligning on Python's changes a shipped Rust signature, so it
        waits for 3.0 with the rest of the breakage. The error codes already
        agree.

5. **Python error types for a wrong argument type are mixed.**
   `BovnarArgumentError` in timekeeping, `datetime_epoch_index`,
   `write_datetime` and the read options; `TypeError` in the literal helpers,
   `parse_type_annotation`, `char_to_digit` and the DOM builder; a raw
   `ctypes.ArgumentError` from `convert_rational`/`convert_value` given a `str`
   unit (and `convert_rational(True, …)` is taken as 1). Pick one rule (likely
   `TypeError` for the wrong type, `BovnarArgumentError` for a bad value) and
   apply it.

    **Done.** One rule, shipped and stated once in doc/09 §11.2: a wrong
        **type** is a `TypeError`, a value the library cannot use is a
        `BovnarArgumentError`, and no `ctypes` error reaches the caller. `_as_vu`
        (unit.py) is now the door for every unit-shaped argument, so
        `convert_value`, `convert_rational`, `unit_to_str`, `units_compatible` and
        the rest answer `TypeError: expected a Unit or a ValueUnit, not str`
        instead of `ctypes.ArgumentError: argument 2: <class 'TypeError'>: wrong
        type`; the two call sites that carried their own copy of the check (dom.py,
        writer.py) lost it. `convert_rational(True, …)` is refused as a bad value:
        `Fraction(True)` is 1, so it had answered as if the caller passed an int.
        `write_array(..., vu=None)` still means "no unit". Tests:
        `TestAUnitShapedArgumentIsATypeMatter` in python/tests/test_unit_object.py.

6. **Recovered documents return quietly.** Under `continue_on_error`,
   `parse_with_options`, `parse_embedded_with_options`, the `stream::*_with_options`
   functions and their Python counterparts return what was recovered without
   an error; only `canonicalize` refuses a recovered read. The errors reach
   `on_error`, where there is one, and `recovery_count` on the reader. Decide
   whether the convenience functions should say so (an error, a flag, or a
   doc line).

    **Decided:** silence is right for the convenience functions — they return
        what the reader could recover, as the C read does — and the count is
        reachable where the object survives the call (`Reader`, `DocumentFeed`,
        `DemuxFeed`), with `canonicalize` the one that refuses a recovered
        document. Recorded in doc/09 §11.6 beside the table, nothing changed. The
        one gap left open: `loads` and `load` build and drop their `Reader`, so a
        caller of those two cannot see the count at all. Closing that is a
        return-value or callback change to two of the most-used functions, not a
        doc line, and it is not part of this pass.

        **Closed 2026-11-13:** as a doc line, and the pass's own pattern -- the
        count belongs to the reader object, and `loads`/`load` build one inside
        the call and drop it. doc/09 §11.6 says so beside the table: a caller who
        needs the count reads through `Reader.read_mem`/`read_stream`. Nothing
        changed in the code.

7. **An exception or panic in `on_error` cannot stop a read by itself.** The C
   `on_error` callback returns nothing, so the binding parks the exception and
   the read stops at the next `on_verified`/`on_unverified`/`want_unit` call,
   or runs to the end with none installed, then re-raises (documented in
   doc/09 and doc/14). Stopping it at once would need a C change: a `bool`
   return, or a stop flag the reader checks.

    **Decided:** keep, documented (doc/09 §11.2's `on_error` row, doc/14
        §10.2), and the C change belongs to 3.0. `bvnr_on_error_fn` returns
        `void`, so a `bool` return changes the callback ABI every existing
        consumer compiles against. The additive alternative — a `stop_on_error`
        flag inside the 16 bytes of `bvnr_read_flags_t._reserved[2]`, which needs
        no size change — was designed and deliberately not taken here: it changes
        the reader's contract (where a read can stop), and that wants its own
        review rather than a bindings pass.

8. **Rust `_with` means three things.** `Reader::read_mem_with` takes
   `ReadHandlers`, `Writer::to_mem_with` positional pretty/unit_flags/
   emit_version, `DocumentFeed::feed_with` handlers per call; `_with_options`
   takes an options struct. `DomDoc::parse_reader_capped_with_policy` sits next
   to `parse_reader_with_policy`. Renaming is source-breaking; settle it before
   the names are released in 2.1.0, or leave it.

    **Decided:** leave. 2.1.0 is on crates.io, so every one of these names is
        public and any rename is a semver break; only additions are compatible, and
        a second spelling for each call is worse than the inconsistency. The
        meanings to write down if 3.0 renames them: `_with` = loose scalars or
        handlers, `_with_options` = an options struct, `_with_policy[_capped]` = a
        `UnitPolicy`. A 3.0 pass should also fix the word order —
        `parse_reader_capped_with_policy` is the odd one.

9. **`canonicalize` with both sides failing.** Rust reports the output's error
   first (it is what stopped the read), Python the input's. Both failing in one
   call is near impossible; align if the code is touched again.

Added by the second review pass the same day (also design choices):

    **Done, as alignment rather than behaviour.** Python now checks the
        output's stashed exception before the input's, as Rust does ("the output's
        failure first: it is what stopped the read"), and doc/09 §4.9 / doc/14
        §4.10 say so. The case is not constructible, which is why this had no test:
        a sink that fails stops the read, so the source is never asked again and
        `source.exc` stays `None` — verified by reverting the order and re-running
        the experiment, which produced the same error. The reorder makes the intent
        match Rust's.

10. **A failing feed and the values before the error.** Rust's
    `ValueFeed::feed` appends the assignments completed before the error;
    Python's `DocumentFeed(values=True)` drops what the failing `feed()`
    completed (doc/09 §4.7). Each is documented; they disagree.

    **Decided:** the difference is inherent to the shape, and doc/09 §4.7
        states it. Rust's `ValueFeed::feed(&mut out)` keeps what the failing call
        completed because the caller owns the buffer; Python's `feed()` *returns*
        its items, so a call that raises cannot also hand them over. Values
        completed by earlier calls are kept in both, and a fatal error ends the feed
        either way, so nothing is lost that could have been read.

11. **Parity gaps nothing documents.** Rust `live::DocumentFeed` has
    `set_unit_policy`, Python's `DocumentFeed` no unit policy at all. Rust has
    `stream::parse_embedded_reader`; Python's `parse_embedded` takes bytes
    only. A mux stream's own key comes out of Rust's `ValueBuilder` as an
    empty `Bytes` value, not out of Python's `DemuxFeed(values=True)` (doc/14
    §4.9 says so now).

    **Partly done.** `DocumentFeed.set_unit_policy` now exists in Python
        (stream.py), so the one live object without a policy has one; doc/09
        §4.7/§5.6 name it, and `test_document_feed_takes_a_unit_policy` holds the
        feed to the `Reader` under the same policy. Left as they are, with a doc
        line each: `parse_embedded` takes bytes or `str` because it has no
        transport to pull from (`canonicalize` is the entry point that reads a
        stream), and a mux stream's own key yields no value in Python's
        `DemuxFeed(values=True)` where Rust's `ValueBuilder` hands out an empty
        `Bytes` — doc/14 §4.9 records the Rust side, doc/09 §4.7 the difference.

12. **float:16/32 subnormals print more digits than they need.**
    `bvn_format_double` takes the precision of a binary32/binary16 subnormal
    from the format's width (24 or 11 bits), not from the bits the subnormal
    has, so 2⁻¹⁴⁹ as `float:32` is `1.4012984643e-45`. It round-trips; the
    shortest form depends on how a reader rounds a `float:32` it reads as a
    double, which is the open question.

    **Decided:** keep, and it is a spec question rather than a binding one. The
        extra digits come from taking the precision of the declared width (24 or 11
        bits) instead of the subnormal's own bit count, and the shorter alternative
        depends on how a reader rounds a `float:32` it reads as a double. The
        current form round-trips, which is the invariant both bindings rely on;
        the shorter one is for the next spec revision to decide, not this pass.

    **Done 2026-11-13.** The digits come from the subnormal's own bit count now:
        `bvn_format_double` packs the narrowed value into binary16 or binary32 and,
        when the exponent field is zero and the significand is not, sets the
        precision to that significand's bit length, so `2**-149` under `float:32`
        is `1.4e-45` and `2**-24` under `float:16` is `6e-8` where both had eight
        digits. Two bits are the floor: at one, the round-trip interval of a
        float with a single significand bit is the whole octave below its value,
        and the search answers `2e-45` -- 43% off the value it is meant to name,
        because `1e-45` falls below the octave's midpoint. Normals are untouched:
        they keep the width's digits, `65504` is still `6.5504e+4`, and the double
        subnormal path (`5e-324`) was already at its own precision. The reading
        side is now a rounding step, which every Bovnar reader already takes:
        doc/08 (bvn_format_double) states it with the `1.4e-45` example, and the
        `format_double` rows of doc/09 and doc/14 carry the short form. Tests:
        the subnormal cases in `test_write_float_narrow_widths`. No float vectors
        encode the old digits, so nothing else moved.

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

- `build/` (including `build/asan/`, `build/clang/` and `build/mingw/`) is
  git-ignored and does not travel. Configure them again:
  `cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo .` and
  `cmake -B build/asan -DBVNR_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug .`, then
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
