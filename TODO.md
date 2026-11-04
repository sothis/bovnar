# TODO

## State 2026-11-04 — 2.0.1 released

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

## 2.0.0 — released earlier the same day

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
