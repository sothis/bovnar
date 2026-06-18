# Bovnar 1.1.0 — native time, opt-in and fully compatible

**Spec 1.1 adds a native time family to the unit-safe format — and nothing
else changes.** Every 1.0 document parses unchanged and decodes to the same
values; the new constructs are **opt-in**, enabled per document by a leading
`#!bovnar 1.1` directive. A 1.0 reader skips the directive as a comment, and a
1.1 reader treats a directive-less document as spec 1.0.

```bovnar
#!bovnar 1.1
.launch  = <datetime:64,tai> 2026-05-28T00:00:00Z;  # epoch-aware timestamp
.created = 2026-06-15T12:00:00.5Z;                   # bare ISO literal → <datetime:64,unix>
.label   = "caf\u{e9}";                              # \u{…} / \xHH string escapes
.cell    = &.matrix[0][1];                           # references can index arrays
```

This is an **additive 1.x release**: per the §17 stability contract, it only
adds new constructs, appends new error codes after the previous maximum, and
adds optional reader/writer flags. No 1.0 document is invalidated.

## Install

```bash
pip install --upgrade bovnar     # pure-ctypes bindings + bundled shared library
```

Binary wheels are published for **CPython 3.10–3.14** on **Linux** (x86_64,
aarch64) and **macOS** (arm64, x86_64); an sdist is available for other targets.

**C / embedding:** vendor the single-file amalgamation `dist/bovnar.h` +
`dist/bovnar.c` (C99, no dependencies), or build the library with CMake. The
build understands the highest spec it supports via `bvnr_spec_version()` /
`BVNR_SPEC_VERSION_MAJOR`·`MINOR` (now `1`·`1`).

## What's new in 1.1

- **Version directive** — an optional first-line `#!bovnar <major>.<minor>`
  comment declaring the spec a document targets. A 1.1 reader records it
  (`bvnr_reader_get_declared_version`, the `bovnar version` CLI) and, with the
  `strict_version` read flag, rejects a version it does not support.
- **Native time family** — `<datetime:width,epoch>` carries a timestamp as a
  **signed integer count of seconds** since a named epoch (`unix` default, plus
  `tai`, `gps`, `mjd`, `ntp`, `galileo`, `glonass`, `y2000`, `beidou`) — distinct
  from a *duration* (a number with a time unit). The epoch is recoverable
  (`bvnr_datetime_epoch_name`/`_mjd`) and convertible to civil time via
  `bvn_datetime.h`. In an array, datetime is its own kind and its epoch is a
  dimension.
- **ISO-8601 datetime literals** — write `YYYY-MM-DD` or
  `YYYY-MM-DDTHH:MM:SS`, with an optional `Z`, a numeric `±HH:MM` offset, and/or
  a fractional second, instead of a raw integer. It converts to the epoch-seconds
  carrier at parse time and round-trips idempotently; a bare literal infers
  `<datetime:64,unix>`. The UTC→epoch conversion is **leap-second correct**
  (`tai` applies the IERS table); the atomic GNSS epochs reject a literal, since
  they have no round-trippable civil⇄seconds inverse — use an integer carrier
  there. A fractional second is preserved verbatim and re-emitted as an ISO
  literal.
- **Richer string escapes** — `\u{1–6 hex}` (a Unicode scalar, UTF-8 encoded)
  and `\xHH` (one byte), both held to the same valid-UTF-8 / no-raw-control rules
  as the rest of the string grammar.
- **Reference array indexing** — a reference path may index arrays,
  `&.matrix[0][1]`, resolved by `bvn_dom_lookup` (and `bovnar query`).
- **Python & numpy** — `bovnar.version()`/`spec_version()`/`peek_version()`,
  `Reader.declared_version`, `Quantity.epoch_name`/`epoch_mjd`,
  `ValueTypeFamily.DATETIME`. `dumps()` auto-prepends `#!bovnar 1.1` (and the
  `<datetime>` annotation) only when the value tree needs it, so typed
  round-trips stay lossless. The numpy bridge maps a unix-epoch datetime array
  to/from `datetime64[s]`.
- **Python — lossless wide/decimal/fixed-point floats** — `Quantity` gains
  `.decimal()`/`.fraction()` (the exact value from the verbatim literal) and
  `.stored_value()`/`.ieee_bits()`/`.fixed_point()` (bit-exact materialisation of
  the 16/32/64/128/256 encodings), plus a `from_number` constructor; `dumps()`
  accepts `Decimal`/`Fraction`. The numpy bridge decodes `float_dec`/`float_fix`/
  `float:128`+ to exact `Decimal` object arrays (with `from_numpy(float_format=)`
  to write them), and the arbitrary-precision `bvn_float` API is exposed as
  `bovnar.BvnFloat`.
- **Tooling** — a `bovnar version` subcommand; the `datetime` keyword in all five
  syntax highlighters and the web playground; and a conformance suite grown to
  **301 cases** (up from 207), adding the `version` and `datetime` groups —
  including the ISO-literal `DTLIT` cases — passing in both self-test and `--iut`
  modes.

## New error codes (appended, non-breaking)

`error_invalid_spec_version` (42), `error_unsupported_spec_version` (43),
`error_invalid_codepoint` (44), `error_invalid_datetime_literal` (45),
`error_datetime_literal_unsupported_epoch` (46). Existing codes are unchanged.

## Compatibility

- **1.0 documents are unaffected** — parse unchanged, decode identically. The
  1.0 freeze (grammar, type families, arrays/structs/octet-streams/references,
  homogeneity rules, error-code values) still holds.
- **1.1 constructs are opt-in** — they require the `#!bovnar 1.1` directive; a
  1.1-only construct in a directive-less (spec-1.0) document is an error, exactly
  as a 1.0 reader reports.

## Documentation

Full docs — tutorial, specification, unit/currency reference, C and Python API,
formal EBNF grammar, FAQ, conformance protocol, and cheat sheet — are available
at **https://www.bovnar.io** (each also downloadable as **PDF**), and in the
[`doc/`](https://github.com/sothis/bovnar/tree/1.x/doc) directory. See
[`CHANGELOG.md`](CHANGELOG.md) for the complete change list.

## License

MIT.
