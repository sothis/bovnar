# Changelog

All notable changes to Bovnar are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/1.1.0/). Versioning is **semantic
versioning of the format (spec)**; the reference implementation
(`BVNR_VERSION_STRING`, `bovnar.__version__`, the CMake project version) tracks
it in lockstep. The highest spec a build understands is reported by
`bvnr_spec_version()` / `BVNR_SPEC_VERSION_MAJOR`·`MINOR`.

## [1.1.0]

Spec 1.1 is **purely additive** over the frozen 1.0 baseline: every spec-1.0
document parses unchanged and decodes to the same values. The new constructs are
**opt-in** — a document enables them with a leading `#!bovnar 1.1` directive; a
document with no directive is treated as spec 1.0, and a 1.1-only construct in
such a document is an error, exactly as a 1.0 reader reports.

### Added

- **Version directive** — an optional first-line `#!bovnar <major>.<minor>`
  comment declaring the spec version a document targets. Lexically it is a
  comment, so a 1.0 reader skips it; a 1.1+ reader records it
  (`bvnr_reader_get_declared_version`, surfaced by `bovnar validate`/`query` and
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
  A malformed or out-of-range literal is `error_invalid_datetime_literal`.
- **Reference array indexing** — a reference path may index arrays,
  `&.matrix[0][1]`. The index is stored verbatim/unresolved at the byte layer and
  interpreted by `bvn_dom_lookup` (which also backs `bovnar query`): a flat
  `/`-row matrix is addressed `[row][col]`, a 1-D array `[i]`, and nested arrays
  descend one index per level.
- **C API** — `bvnr_version()`, `bvnr_version_string()`, `bvnr_spec_version()`,
  `bvnr_reader_get_declared_version()`, `bvnr_peek_version()`,
  `bvnr_write_version()`, `bvnr_write_datetime()`, `bvnr_datetime_epoch_name()`,
  `bvnr_datetime_epoch_mjd()`, `bvnr_datetime_epoch_index()`; the
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
  **303 cases** (groups `version`, `datetime` — including the ISO-literal
  `DTLIT` cases — plus escape/reference additions), passing in both self-test
  and `--iut` modes.

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
  Consumers no longer have to add `-lm` when linking `libbvnr_static`.
- **Dropped the optional `libgmp` build-time dependency.** It was used only by an
  `#ifdef WITH_GMP` cross-check in `bvnr_int_test`; the bignum tests already cover
  parsing, formatting, and a 32768-bit round-trip without it. The cross-check, the
  `FindGMP.cmake` module, and its CMake wiring are removed, so the project now has
  no external library dependencies at build time or runtime.

### Fixed

Hardening uncovered while developing 1.1:

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
