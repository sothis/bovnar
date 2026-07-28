# Contributing to Bovnar

Bug reports, questions, documentation fixes, portability patches, format
proposals and third-party implementations are all welcome.

Participants are expected to follow the [Code of Conduct](CODE_OF_CONDUCT.md).

---

## Table of Contents

- [Format changes and implementation changes](#format-changes-and-implementation-changes)
- [Getting Support](#getting-support)
- [Reporting a Bug](#reporting-a-bug)
- [Reporting a Security Issue](#reporting-a-security-issue)
- [Proposing a Change](#proposing-a-change)
- [Development Setup](#development-setup)
- [Before You Open a Pull Request](#before-you-open-a-pull-request)
- [Generated Files](#generated-files)
- [Code Style](#code-style)
- [Documentation](#documentation)
- [Commit Messages](#commit-messages)
- [Licensing](#licensing)

---

## Format changes and implementation changes

Bovnar is a library and a specification. The specification is versioned
(`doc/03_bovnar_spec.md`), has a formal grammar (`doc/12_bovnar.ebnf`), a
registered IANA media type (`text/vnd.bovnar`), and a 387-case conformance suite
that third-party parsers can be measured against.

A document that parses under a given spec version must keep parsing, and must
keep meaning the same thing. Changing the library is an ordinary code change.
Changing what the grammar accepts, or what a value means, is a specification
change and is gated behind a spec version directive (`#!bovnar 1.1`). The two
are reviewed differently — see [Proposing a Change](#proposing-a-change) — so
please do not combine them in one pull request.

## Getting Support

The documentation covers most usage questions:

| Question | Where |
|---|---|
| How do I use it? | [`doc/01_bovnar_tutorial.md`](doc/01_bovnar_tutorial.md) |
| Why does it behave this way? | [`doc/02_bovnar_faq.md`](doc/02_bovnar_faq.md) |
| What is legal syntax? | [`doc/03_bovnar_spec.md`](doc/03_bovnar_spec.md), [`doc/12_bovnar.ebnf`](doc/12_bovnar.ebnf) |
| Which unit spelling means what? | [`doc/04_bovnar_unit_cheatsheet.md`](doc/04_bovnar_unit_cheatsheet.md), [`doc/07_bovnar_unit_ambiguities.md`](doc/07_bovnar_unit_ambiguities.md) |
| When is a unit rejected or converted? | [`doc/06_bovnar_unit_policy.md`](doc/06_bovnar_unit_policy.md) |
| C API | [`doc/08_bovnar_readwrite_api.md`](doc/08_bovnar_readwrite_api.md) |
| Python bindings | [`doc/09_bovnar_python_bindings.md`](doc/09_bovnar_python_bindings.md) |

Otherwise, open an issue at <https://github.com/sothis/bovnar/issues>. Questions
that the documentation should have answered are treated as documentation bugs.

## Reporting a Bug

Open an issue at <https://github.com/sothis/bovnar/issues> and include:

1. **`bovnar version` output** — the library version and the supported spec version.
2. **Platform and compiler** — OS, architecture, and `gcc --version` /
   `clang --version` / MSVC version.
3. **A minimal `.bvnr` document** that reproduces the problem. The parser is a
   state machine over bytes, so a short reproducer usually identifies the state
   involved.
4. **Expected and actual behaviour** — for a parse problem, the error code
   (`error_unit_mismatch`, `octet_stream_out_of_sync`, …) and the reported line
   and column.

`bovnar events <file>` prints the unverified (lexer) and verified (validator)
event streams side by side, which shows where the two diverge. Its output is
useful in most parsing bug reports.

If the document contains an octet stream, attach the file rather than pasting
it: a binary payload does not survive copy-paste, and a corrupted one reproduces
a different problem.

## Reporting a Security Issue

Do not open a public issue for a memory-safety problem, a crash on
attacker-controlled input, or any other exploitable defect. Email
**bovnar@mail.de** with the details and a reproducer, and allow time for a fix
before disclosing.

The parser is intended to be pointed at untrusted bytes arriving over a socket.
Fuzz findings reachable from `bvnr_read()` on hostile input are handled as
security reports.

[`SECURITY.md`](SECURITY.md) has the full policy: what to include, which
versions are supported, and — the part worth reading before writing a
report — what is a documented property of the format rather than a defect.
§18 of the specification is the normative side of that list.

## Proposing a Change

**Implementation changes** — bug fixes, portability work, performance, error
messages, tests, documentation — need no preliminaries. Open an issue first if
the change is large enough that you would rather not write it twice; otherwise
send a pull request.

**Format changes** are better discussed in an issue before code is written. A
format change is anything that alters which documents parse, or what a parsed
document means: a new type family, unit, currency, epoch or literal form, a
changed default, or a relaxed or tightened validation rule.

A format change touches all of the following, and a pull request missing any of
them cannot be reviewed as a whole:

- `doc/03_bovnar_spec.md` — the normative rule
- `doc/12_bovnar.ebnf` — the grammar
- the conformance corpus in `tests/`, with cases for both the accepting and the
  rejecting side, and `doc/13_bovnar_conformance.md` updated to match (a CTest
  gate compares the document against what the adapter emits)
- the C implementation, the Python bindings, and the CLI, where each is affected
- `CHANGELOG.md`
- version gating, if the change is not backward compatible: new syntax must be
  reachable only from a document that opts in with `#!bovnar <major>.<minor>`,
  and unreleased work is marked "under implementation" rather than described as
  shipped

**New units and currencies** follow a shorter path. The registries are generated
from `src/gendata/*.bvnr`, which are themselves Bovnar documents, so adding one
is a data change plus a regeneration rather than a code change. See
[Generated Files](#generated-files).

## Development Setup

Requirements: CMake ≥ 3.21, a C99 compiler, and Python 3. Python is optional,
but without it the Python binding tests and the documentation checks are not
registered at configure time.

```bash
cmake -B build .
cmake --build build -j"$(nproc)"
cd build && ctest --output-on-failure
```

Or the wrapper at the repository root, which also runs the fuzz harnesses:

```bash
./run_tests.sh                 # --no-fuzz to skip them
```

Label filters narrow a run while iterating:

```bash
ctest -L unit                  # C unit tests
ctest -L conformance           # the 387-case suite
ctest -L cli                   # command-line behaviour
ctest -L python                # bindings (needs libbvnr.so; CTest injects the path)
ctest -L fuzz_deep             # the long fuzz tier
```

To run the Python tests directly:

```bash
export LIBBOVNAR_PATH="$PWD/build/libbvnr.so"
pip install -e ".[dev]" && pytest python/tests -v
```

`pyproject.toml` sits at the repository root, not under `python/`, so the
editable install runs from the root; `python/bovnar` is mapped into the wheel
from there.

To enable the repository's git hooks, once per clone (git does not track
`.git/hooks`):

```bash
git config core.hooksPath githooks
```

## Before You Open a Pull Request

CI runs all of the following; running it locally is faster than waiting for it.

```bash
# 1. Warnings as errors, as in CI.
cmake -B build -DBVNR_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo .
cmake --build build -j"$(nproc)" && ctest --test-dir build --output-on-failure

# 2. ASan + UBSan. The Python tests cannot run here: they load libbvnr.so
#    through ctypes into an unsanitized interpreter, which ASan refuses.
cmake -B build-asan -DBVNR_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug .
cmake --build build-asan -j"$(nproc)"
ctest --test-dir build-asan -LE python --output-on-failure

# 3. The fuzz harnesses.
./run_tests.sh
```

Besides the unit, integration, conformance and fuzz tests, `ctest` runs every
file in `examples/` through the CLI, checks that the generated amalgamation is
reproducible, checks that the WebAssembly artifact has not drifted behind the
library, and runs the documentation checks described below.

Please include a test with a fix. A bug that reached a release did so because
nothing pinned the behaviour.

## Generated Files

Some files are build products — a few tracked, others regenerated into
`build/generated/` on each build. Each carries a `DO NOT EDIT` banner naming its
generator. A hand edit survives until the next regeneration, so change the
source and re-run the generator instead.

| Generated | Source of truth | Generator |
|---|---|---|
| `include/bovnar_units.gen.h` | `src/gendata/units.bvnr` | `gen_units.py` |
| `include/bovnar_si_prefix.gen.h`, `include/bovnar_iec_prefix.gen.h` | `src/gendata/prefixes.bvnr` | `gen_prefixes.py` |
| `include/bovnar_profiles.gen.h` | `src/gendata/ucum.bvnr`, `unece.bvnr`, `qudt.bvnr`, `qudt-qk.bvnr`, `udunits.bvnr` | `gen_profiles.py` |
| `build/generated/bovnar_currency_table.gen.inc` | `src/gendata/currencies.bvnr` | `gen_currencies.py` |
| `python/bovnar/_pint_units.py` | the unit registry | `gen_units.py` |
| `web/docs/*.html` | `doc/*.md` | `gen_html_docs.py` |
| `web/sitemap.xml`, the LLM views, the translated pages | `web/` sources | `gen_sitemap.py`, `gen_llms.py`, `gen_i18n.py` |

The generated web pages are committed, so an edit to `doc/*.md` that does not go
through `gen_html_docs.py` leaves the tree inconsistent. CI detects this.

## Code Style

The surrounding file is the authority. In summary:

- **C99**, with **no dependency beyond the C standard library**, not even
  `libm`. This is a hard constraint: it is what allows the single-file
  amalgamation and the WebAssembly build. A patch introducing a library
  dependency will be asked to remove it.
- **Tabs for indentation**, K&R bracing with a function's opening brace on its
  own line, roughly 80 columns.
- Every source file carries the SPDX header (`SPDX-License-Identifier: MIT`)
  used across the tree.
- Public API in `include/`; implementation-internal headers sit beside their
  code as `src/<area>/bvn_*_impl.h`.
- Python is 4-space indented and targets plain CPython 3 with no compiled
  extension. The bindings are pure `ctypes` by design; NumPy and Pint are
  optional bridges and must stay optional.
- Comments explain why rather than what. Where a patch makes a non-obvious
  choice, the comment recording the reason is part of the patch.

`libbvnr.so` carries a versioned soname, and `bvnr_abi_dump` records public
struct layouts. If a change alters a public type's layout or an exported
signature, note it in the pull request: it is an ABI break and requires a soname
bump.

## Documentation

Several CTest gates check the documentation:

- `check_doc_layout.py` — every document in `doc/` has the same shape, and its
  table of contents lists the sections it contains.
- `check_doc_refs.py` — every `§N.M` cited from a code comment resolves to a
  section that exists.
- `check_conformance_doc.py` — the worked examples in `doc/13` match what the
  conformance adapter emits.
- `check_web_links.py`, `check_release_links.py` — internal site links resolve,
  and the download menu points at the current release.

Renumbering a section therefore requires moving the citations that point at it;
CI reports the ones that were missed.

## Unit profiles

Editing a table under `src/gendata/*.bvnr` changes what a foreign unit code
means, and `gen_profiles.py` only checks that a `.bovnar` target names something
this build's registry has — not that the code is worth what the table says.

`check_profile_factors.py` is the outside check. It covers all five profiles:
`ucum`, `udunits`, `qudt` and `qudt-qk` against their own publishers, and
`unece` at one remove through QUDT's `uneceCommonCode` cross-reference, because
Rec 20 states its conversion factors in prose. A `unece` disagreement is
therefore evidence that one of two tables is wrong, not proof of which:

```
python3 check_profile_factors.py --fetch     # once, populates <build>/vocab/
python3 check_profile_factors.py --verbose   # every row, not just the failures
```

It compares each mapped row against the publisher's own definitions, using the
built library for the native side. Without the cache the CTest gate
(`bvnr_profile_factors`) **skips green**, because a test must not depend on the
network — so run it yourself after touching any of those four data files, and
pass `--strict` in CI where the fetch has run. See doc/11 §9.5 for what it does
and does not prove.

## Commit Messages

Subjects follow a lowercase `scope: what changed and why`, written as a
statement:

```
cli: plain notation for short floats, and --text-only to refuse a binary region
docs: check the documentation against the implementation, not against itself
ucum: the profile was missing from the grammar, the corpus and the changelog
```

Common scopes: `spec`, `docs`, `cli`, `units`, `profiles`, `web`, `python`,
`tests`, `ietf`, `publish`. Keep one logical change per commit, and put the reasoning in the body
when the subject cannot carry it.

## Licensing

Bovnar is MIT licensed. By contributing you agree that your contribution is
licensed under the same terms. There is no CLA.

When adding a new file, copy the SPDX header from an existing one and add your
own copyright line; you retain copyright in what you write.
