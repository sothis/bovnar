# Changelog

All notable changes to Bovnar are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning is **semantic
versioning of the format (spec)**; the reference implementation
(`BVNR_VERSION_STRING`, `bovnar.__version__`, the CMake project version) tracks
it in lockstep. The highest spec a build understands is reported by
`bvnr_spec_version()` / `BVNR_SPEC_VERSION_MAJOR`·`MINOR`.

## [1.1.0] - 2026-06-19

Spec 1.1 is **purely additive** over the frozen 1.0 baseline: every spec-1.0
document parses unchanged and decodes to the same values. The new constructs are
**opt-in** — a document enables them with a leading `#!bovnar 1.1` directive; a
document with no directive is treated as spec 1.0, and a 1.1-only construct in
such a document is an error, exactly as a 1.0 reader reports.

The one reserved-prefix exception to "parses unchanged": the `#!bovnar ` prefix
on a first-line comment is now read as a version directive, so a 1.0 document
whose first line is a comment beginning literally with `#!bovnar ` followed by a
non-version remainder is reported as a malformed directive rather than ignored.
`#!bovnar` was never a 1.0 convention, so this affects no realistic document.

### Added

- **Version directive** — an optional first-line `#!bovnar <major>.<minor>`
  comment declaring the spec version a document targets. Lexically it is a
  comment, so a 1.0 reader skips it; a 1.1+ reader records it
  (`bvnr_reader_get_declared_version`, surfaced by `bovnar validate` and
  the CLI `bovnar version`) and, with the `strict_version` read flag, rejects a
  version it does not support (`error_unsupported_spec_version`). A malformed
  directive is `error_invalid_spec_version`.
- **Richer string escapes** — `\u{1–6 hex}` (a Unicode scalar, UTF-8 encoded)
  and `\xHH` (one byte). A `\u{…}` surrogate or value above `U+10FFFF` is
  `error_invalid_codepoint`; `\x` must leave the string valid UTF-8
  (`\xC3\xA9` = "é", a lone `\xFF` is `error_invalid_utf8_byte`), and neither may
  smuggle an ASCII control byte past the rule that rejects raw control bytes
  (the whitespace controls remain available via the named escapes).
- **Native time family** — `<datetime:width,epoch>`: a timestamp carried as a
  **signed integer count of seconds** since a named epoch (`unix` default, `tai`,
  `gps`, `mjd`, `ntp`, `galileo`, `glonass`, `y2000`, `beidou`), distinct from a
  *duration* (a number with a time unit, e.g. `<float:64,s>`). The carrier
  validates like `sint`; the epoch is recovered with `bvnr_datetime_epoch_name`/
  `bvnr_datetime_epoch_mjd` and converted to civil time with the `bvn_datetime.h`
  helpers. In an array, datetime is its own kind and its epoch is a dimension
  (mixing epochs, or datetime with a plain number, is `error_array_element_type_mismatch`).
- **ISO-8601 datetime literals** — a value may be written as `YYYY-MM-DD`,
  `YYYY-MM-DDTHH:MM:SS`, with an optional trailing `Z`, a numeric `±HH:MM`
  time-zone offset, and/or a fractional second, instead of a raw integer; it is
  converted to the epoch-seconds carrier at parse time (the integer is what is
  stored and re-emitted, so round-trips stay idempotent). A bare literal with no
  annotation infers `<datetime:64,unix>`. A `±HH:MM` offset folds the written
  civil time to true UTC before the conversion (and before `tai`'s leap-second
  lookup, so atomic values stay correct). A fractional second (any digit count —
  ISO 8601 sets no limit) leaves the whole-second carrier unchanged, but the
  verbatim digits are preserved: consumers read them as a string
  (`bvnr_data_t.frac_data`/`frac_length`, or `bvn_dom_get_datetime_fraction()`),
  and a value carrying a fraction is pretty-printed back as an ISO literal
  (`<datetime:64> 2026-06-15T12:00:00.5Z`) so it round-trips idempotently. The UTC→epoch
  conversion is leap-second correct: civil epochs (`unix`/`mjd`/`ntp`/`y2000`)
  use the uniform scale and `tai` applies the IERS leap-second table; the atomic
  GNSS epochs (`gps`/`galileo`/`glonass`/`beidou`) reject a literal
  (`error_datetime_literal_unsupported_epoch`) because there is no
  round-trippable civil⇄seconds inverse for them — use an integer carrier there.
  A second of `60` is accepted as a UTC leap second; since the carrier is whole
  epoch-seconds it normalises onto the following second (`2016-12-31T23:59:60Z`
  and `2017-01-01T00:00:00Z` store the same `unix` value), the correct POSIX
  reading. A malformed or out-of-range literal is `error_invalid_datetime_literal`.
- **Reference array indexing** — a reference path may index arrays,
  `&.matrix[0][1]`. The index is stored verbatim/unresolved at the byte layer and
  interpreted by `bvn_dom_lookup` (which also backs `bovnar query`): a flat
  `/`-row matrix is addressed `[row][col]`, a 1-D array `[i]`, and nested arrays
  descend one index per level.
- **C API** — `bvnr_version()`, `bvnr_version_string()`, `bvnr_spec_version()`,
  `bvnr_reader_get_declared_version()`, `bvnr_peek_version()`,
  `bvnr_write_version()`, `bvnr_write_datetime()`, `bvnr_datetime_epoch_name()`,
  `bvnr_datetime_epoch_mjd()`, `bvnr_datetime_epoch_index()`,
  `bvnr_canon_observer_set_version()` (so a canonicalising observer can re-emit
  the source's version directive); the
  `bvnr_read_flags_t.strict_version` and `bvnr_write_flags_t.emit_version` flags;
  the `BVNR_SPEC_VERSION_MAJOR`/`MINOR` macros; the `vt_datetime` family; and
  error codes `error_invalid_spec_version` (42), `error_unsupported_spec_version`
  (43), `error_invalid_codepoint` (44), `error_invalid_datetime_literal` (45),
  `error_datetime_literal_unsupported_epoch` (46).
- **Python bindings** — `bovnar.version()`, `spec_version()`, `peek_version()`,
  `Reader.declared_version`, `Quantity.epoch_name`/`epoch_mjd`,
  `ValueTypeFamily.DATETIME`; `dumps()` automatically prepends `#!bovnar 1.1`
  (and emits the `<datetime>` annotation) when the value tree needs it, so typed
  round-trips are lossless. The numpy bridge maps a unix-epoch datetime array
  to/from `datetime64[s]` (`to_numpy`/`from_numpy`/`array_to_bvnr`); a non-unix
  epoch is refused with a pointer to `dtype='int64'` for the raw seconds, NaT is
  rejected, and `array_to_bvnr` prepends the `#!bovnar 1.1` directive.
- **Python — lossless access to decimal, fixed-point and wide binary floats.**
  `Quantity` gains `.decimal()` / `.fraction()` (exact value from the verbatim
  literal), `.stored_value()` / `.ieee_bits()` / `.fixed_point()` (bit-exact
  IEEE materialisation for the 16/32/64/128/256 encodings), and a `from_number`
  constructor; `dumps()` accepts `Decimal` / terminating `Fraction`. The NumPy
  bridge decodes `float_dec` / `float_fix` / `float:128`+ to exact `Decimal`
  object arrays (typed path; `dtype='float64'` is the lossy escape) and
  `from_numpy` gains `float_format=` to write them. The arbitrary-precision
  `bvn_float` API is exposed as `bovnar.BvnFloat`. Materialisation covers the
  full representable range via pure-Python encoders past the parser's cap.
- **Tooling** — `bovnar version` subcommand; `datetime` keyword in all five
  syntax highlighters and the web playground; the conformance suite grew to
  **306 cases** (groups `version`, `datetime` — including the ISO-literal
  `DTLIT` cases — plus escape/reference additions), passing in both self-test
  and `--iut` modes.
- **Windows build support (64-bit MinGW64 and MSVC).** The library (`bvnr.dll` +
  import lib, plus a static archive) and the `bovnar` CLI now build on Windows.
  A portability shim (`src/utils/bvn_port.h`) maps the fd I/O onto the CRT and —
  critically for a binary format — forces binary mode on opened files and on
  stdin/stdout/stderr so no CRLF translation can corrupt the byte stream; the
  bench timing uses `QueryPerformanceCounter`/`GetThreadTimes`. DLL symbols are
  exported via CMake `WINDOWS_EXPORT_ALL_SYMBOLS` (matching the export-all-extern
  behaviour on ELF). On MSVC the static archive is `bvnr_static.lib` (the DLL
  import lib takes `bvnr.lib`); MinGW keeps the unified `libbvnr` names. A new
  `Build & Package` CI workflow builds both Windows toolchains and a native Linux
  target, smoke-tests the CLI, and publishes the build + amalgamation artifacts.
  The POSIX test harness (fork/exec IUT, socketpair, sigaction) is not built on
  Windows.
  The CLI's decorative UTF-8 UI (events/bench tables, box-drawing rules) renders
  on a real Windows console via `WriteConsoleW` — the CRT's byte path garbles
  multi-byte UTF-8 even with the console code page set to UTF-8 — while piped or
  redirected output stays byte-exact raw UTF-8.

### Changed

- Library/package version bumped to **1.1.0**; spec 1.1 ships as the additive
  successor to the frozen 1.0 baseline, which remains the stable floor every 1.x
  release must honour.
- **Dropped the `libm` dependency.** The library never used a transcendental
  function — every `pow()`/`log10()` call raised to or recovered an *integer*
  power, so they are replaced with integer exponentiation helpers (`bvni_ipow`,
  `bvni_pow10`) that are bit-exact for powers of two and for powers of ten in the
  exactly-representable range. The remaining `<math.h>` uses (`fabs`, `isfinite`,
  `isinf`, `isnan`, `signbit`) are compiler builtins that need no runtime library.
  Consumers no longer have to add `-lm` when linking `libbvnr`.
- **Dropped the optional `libgmp` build-time dependency.** It was used only by an
  `#ifdef WITH_GMP` cross-check in `bvnr_int_test`; the bignum tests already cover
  parsing, formatting, and a 32768-bit round-trip without it. The cross-check, the
  `FindGMP.cmake` module, and its CMake wiring are removed, so the project now has
  no external library dependencies at build time or runtime.
- **Unified library artifact name.** Both libraries now share the base name
  `bvnr`, differentiated only by extension: the static archive is `libbvnr.a`
  and the shared object is `libbvnr.so` (was `libbvnr_static.a` /
  `libbvnr_shared.so`). Link with `-lbvnr` (this resolves to the shared object
  when both are present; force the archive with `-l:libbvnr.a` or static-link
  flags). The CMake target names `bvnr_static`/`bvnr_shared` are unchanged.
- **Versioned soname for the shared library.** A system/distro build now installs
  `libbvnr.so.1` (SOVERSION 1) so the dynamic loader can detect an ABI
  mismatch — relevant because public by-value structs grew this release
  (`bvnr_data_t` gained `frac_*`, `bvnr_read_flags_t` gained `strict_version`),
  appended so offsets are preserved but requiring consumers to recompile against
  the 1.1 headers rather than mixing a 1.0-compiled object with the new library.
  The Python wheel keeps the unversioned `libbvnr.so` it loads by name.
- **Installable build (`cmake --install`).** A non-wheel build now installs the
  shared and static libraries (with the `libbvnr.so.1` soname chain), the `bovnar`
  CLI, and all public headers, plus a **pkg-config** file (`pkg-config --cflags
  --libs bvnr`) and a **CMake package** (`find_package(bovnar)` →
  `bovnar::bvnr` / `bovnar::bvnr_static`). Previously only the freedesktop MIME
  entry was installed, so the soname/headers were built but never laid down.
- **Conformance output upgraded to TAP version 14.** The 306 cases are now grouped
  into TAP 14 *subtests* — one indented child stream per case group (21 groups),
  each with its own plan and rolled up into a parent test point — so the report is
  hierarchical instead of a flat list. The parent plan counts groups (`1..21`);
  individual case points and YAML failure diagnostics are emitted inside each
  subtest. Self-test and `--iut` modes and the exit-code contract are unchanged.

### Fixed

Hardening uncovered while developing 1.1:

- CLI `query` now prints every byte of an octet stream as `\xHH` instead of only
  the first byte (it was silently truncating the value).
- CLI `convert` now reports a usage error when `--from`/`--to` is given without a
  value (previously the flag was misread as the input filename), and refuses to
  emit output larger than 4 GiB rather than skipping the homogeneity/unique-key
  re-validation that backs its "no unrepresentable output" contract.
- CLI `bench --size` rejects a zero or out-of-range value instead of letting the
  benchmark buffer's size arithmetic overflow.
- Thread-safety: the lexer's run-LUT initialisation now runs before `main()` on
  MSVC too (via a `.CRT$XCU` registration), matching the GCC/Clang constructor.
  Previously the MSVC build initialised the tables lazily on first reader use,
  which could race if two threads created their first reader concurrently.
- The streaming demux now rejects a declared message length exceeding `SIZE_MAX`
  on 32-bit hosts, mirroring the document-stream path.
- Documented the ownership and error contracts in `bovnar_dom.h` and `bvn_int.h`
  (which functions consume/borrow/own their arguments and results, and the
  "returns false, leaves out-param unchanged" convention) — the previous headers
  left several leak/double-free traps undocumented.
- Python `dumps()` spec-1.1 directive detection (`_uses_spec_1_1`) now reads a
  Quantity's `vtype.family` defensively, so a Quantity built (via the low-level
  constructor) with an unexpected `vtype` reports "not a datetime" instead of
  raising `AttributeError` mid-serialisation.
- The datetime epoch index was misread as a numeric base in the DOM/parse layer
  (`<datetime:gps> 1010` decoded as base-2); now always decimal.
- Reader/writer validation symmetry for datetime: the writer now emits the
  width and epoch, and range-checks the value and width exactly as the reader
  does (no longer emits a datetime the reader would reject).
- An empty reference index `&.m[]` is rejected by the lexer, matching DOM
  resolution and `bvn_validate_reference`.
- Restored the `-DBVNR_WERROR=ON` (`-fanalyzer`) build.
- Python `dumps()` no longer drops a datetime annotation or crashes on a typed
  array; exposes the epoch helpers.
- `to_dec*` lost precision when encoding a decimal float near a format's Emax:
  `bvnf_dec_render_roundodd` estimated the decimal exponent with a coarse
  `log10(2)` approximation (`0.302`) whose error, past ~2000 bits of binary
  exponent, exceeded the digit-count safety margin and truncated the rendered
  coefficient — decimal128's 34-digit maximum encoded to ~20 significant digits.
  Now uses `0.30103`, exact across the whole exponent range.
- Python NumPy/pint bridge hardening: reject `null` elements that a `bool`/`str`
  dtype would silently coerce; key the pint reverse-map cache on the registry
  (`WeakKeyDictionary`) to avoid `id()`-reuse aliasing and a leak; `to_pint_array`
  rejects datetime arrays; raw Python ints past int64 widen to uint64/object
  instead of raising a bare `OverflowError`; `from_numpy` rejects masked arrays;
  clearer errors for invalid `unit=` / non-pint / mixed unit-and-dimensionless
  inputs.
- **Base-10 rendering of a wide-magnitude float looped forever.** The same coarse
  `log10(2)` (`0.302`) estimate also drove the sibling renderer `bvnf_to_str_dec`;
  once the binary exponent dwarfed the precision it placed the whole digit window
  above the value, and the leading-zero strip then rotated zeros without
  terminating — an infinite loop reachable through `bvn_float_to_str` /
  `bvnr_write_bvnf` (e.g. `to_str(1e4900)` at 16-bit precision). Now uses
  `0.30103` and bounds the strip loop.
- **A `datetime` wrongly accepted an inline unit.** `<datetime:64> 100 m` parsed
  (the "no unit" check was gated on the ISO-literal form only) and the unit was
  then silently dropped on emit; an inline unit on any datetime carrier is now
  `error_unit_illegal`.
- **Non-rectangular sibling arrays of equal cell count were accepted.** A 2×3 and
  a 3×2 block (both six cells) compared equal because the DOM shape check used
  only the flattened cell count; it now also compares the row geometry, so the
  mismatch is caught (`error_array_row_size_mismatch`).
- **An inline unit was dropped when the value also had an explicit (unit-less)
  annotation.** `<float:64> 9.81 m/s` canonicalised to `<float:64> 9.81`, silently
  changing the decoded value; the serializer now re-appends the inline unit.
- **An unbalanced `ev_struct_end` on the canon-observer path** underflowed the
  indent depth, amplifying the next indent to ~4 billion tab bytes; it is now
  rejected, matching the array-depth guard.
- **The JSON converter silently dropped a datetime's sub-second fraction.**
  `bovnar convert` to JSON now refuses a fractional datetime (the integer carrier
  cannot represent it) with a diagnostic rather than truncating, and `bovnar
  query` emits the faithful ISO literal instead of the bare carrier.
- **Python — lossless typed datetime/symbol/reference round-trips.**
  `loads(typed=True)`→`dumps()` dropped a datetime's sub-second fraction, emitted
  an illegal `<datetime:…,no_unit>` annotation for a bare ISO literal, and
  downgraded a symbol or reference to a quoted string; all now round-trip, and
  `dumps()` prepends `#!bovnar 1.1` for a reference that indexes an array
  (`&.a[0][1]`). The numpy bridge no longer silently truncates a DOM
  decimal/wide-binary float under `dtype=object` — it errors, pointing at
  `loads(typed=True)` for exact `Decimal`s (the lossy `dtype='float64'` opt-in is
  unchanged).
- **Tooling** — the Vim highlighter now colours ISO-8601 datetime literals (it
  mis-tokenised them as `YYYY`-dash-`MM` integers, and not at all inside arrays
  or structs); the cibuildwheel test phase now hard-fails if a built wheel cannot
  load its bundled library (the lib-dependent tests are skipif, so they would
  otherwise pass-as-skipped).
- **The canonicalising observer dropped the version directive.**
  `bvnr_canon_observer_*` re-emitted only the value stream, so canonicalising a
  spec-1.1 document (datetime, the new escapes, …) produced output a reader then
  rejected as 1.0. It can now be told the source's declared version
  (`bvnr_canon_observer_set_version`, wired into the `events -d` debug dump), and
  emits the directive ahead of the first event so the canonical form re-reads.
- **CLI `validate` reopened the input file for its second (DOM homogeneity)
  pass.** It now rewinds and reuses the one descriptor, so both passes see the
  same bytes (closing a reopen TOCTOU window) and there is a single fd to manage.
- **`bvn_int_getbit`/`bvn_int_setbit`** now reject a negative bit index up front
  instead of relying on word-index bounds to mask the otherwise-undefined shift.
- **Python** — `make_data_key`'s return annotation was `BvnrData` though it
  returns a `(data, raw)` tuple; corrected, and an unused writer import dropped.

Second release-review pass:

- **Error recovery dropped a value's unit serial when unwinding an array.** The
  `]` and `}` resync paths restored the parent frame's type and unit but not its
  `parsed_unit_serial`, so a later element boundary could mis-decide unit
  restoration and emit wrong unit metadata on recovered (`continue_on_error`)
  output; both paths now restore it, matching the `;` resync and normal
  array-outro paths.
- **MinGW64 file I/O used a 32-bit `off_t`/`lseek`.** Only the MSVC shim widened
  them, so the CLI's 4 GiB file-size guard could never fire on MinGW (and a
  >2 GiB seek wrapped); both Windows toolchains now share the 64-bit `off_t` /
  `_lseeki64` remap.
- **A non-blocking socket source/sink failed the whole parse on `EAGAIN`.** The
  fd backend now waits for readiness via `poll()` (POSIX) on
  `EAGAIN`/`EWOULDBLOCK` instead of treating a would-block as a hard error.
- **Duplicate-key enforcement was skipped under memory pressure.** When the
  temporary key array could not be allocated, `bvn_dom_keys_unique` returned
  "OK"; it now falls back to an in-place O(n²) compare so a document with
  duplicate keys is rejected regardless of available memory.
- **A value-side inline unit ignored the writer's unit flags and was dropped
  silently when over-long.** The serializer now renders it with
  `bvn_unit_to_string_ex` (honouring reduce / ASCII-exponent, as the annotation
  path does) and treats an overflow as an error rather than emitting the value
  without its unit.
- **Hardening:** the streaming varint decoder now rejects an overlong 10th byte
  instead of truncating it; the DOM integer `strtoll`/`strtoull` fallback
  width-clamps its result so a node's tag and payload cannot disagree; the GNSS
  week→epoch helper takes the TAI offset as a parameter (no longer assumes
  GPS/Galileo's +19 for any future constellation); Easter-relative holiday dates
  near the maximum supported year no longer return a stale date with success; and
  the JSON converter guards its output-size multiply against overflow on a 32-bit
  `size_t`.

### Known gaps / deferred

- ISO-8601 datetime literals carry a whole-second integer (a fractional part is
  preserved verbatim and round-tripped, but does not participate in the value's
  arithmetic) and cover `Z` and numeric `±HH:MM` offsets; literals for the atomic
  GNSS epochs are still rejected (no round-trippable inverse).
- The browser playground parser (`web/bovnar_parser.js`) recognises ISO-8601
  literals, infers the `<datetime:64,unix>` default for a bare literal, and
  surfaces the epoch — but, by its lenient design, it still displays literals as
  written without converting them to epoch seconds or leap-second/calendar-
  validating them; the C reader remains the authority for both.

See [`RELEASE_NOTES_v1.1.0.md`](RELEASE_NOTES_v1.1.0.md) for the full notes.

## [1.0.1]

Implementation maintenance over 1.0.0 — no format change; every 1.0.0 document
is unaffected.

## [1.0.0]

First stable release and the **format freeze**: a document valid under spec 1.0
stays valid, and decodes to the same values, under every 1.x release. This
covers the lexical grammar, the seven type families and their annotations,
arrays (including the homogeneity rules), structs, octet streams, references,
and the error-code values. See [`RELEASE_NOTES_v1.0.0.md`](RELEASE_NOTES_v1.0.0.md)
for the full notes.

[1.1.0]: https://github.com/sothis/bovnar/releases/tag/v1.1.0
[1.0.1]: https://github.com/sothis/bovnar/releases/tag/v1.0.1
[1.0.0]: https://github.com/sothis/bovnar/releases/tag/v1.0.0
