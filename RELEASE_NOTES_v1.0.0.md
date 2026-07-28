# Bovnar 1.0.0 — first stable release

**Unit-safe serialization for science, industry, and finance.** Every value in a
`.bvnr` document carries its type family, bit-width, numeric base, and a
**validated physical unit or currency** — inline, in the same byte stream, with
no external schema. A bare `9.81` is never ambiguous, and a mismatched unit is a
parse error.

```bovnar
.thrust   = <float:64,k~N> 7.6;       # 7.6 kilonewtons
.altitude = <uint:32,m>    412000;    # 412 km, explicit width + unit
.price    = <float_dec:64,$USD> 19.99;
.matrix   = [1, 2, 3]/[4, 5, 6];      # rectangular, homogeneous
```

This is the **1.0 format freeze**: a document valid under spec 1.0 stays valid,
and decodes to the same values, under every 1.x release.

## Install

```bash
pip install bovnar          # pure-ctypes bindings + bundled shared library
```

Binary wheels are published for **CPython 3.10–3.14** on **Linux** (x86_64,
aarch64) and **macOS** (arm64, x86_64); an sdist is available for other targets.

**C / embedding:** vendor the single-file amalgamation — `bovnar.h` + `bovnar.c`
(C99, no dependencies), regenerated into `build/amalgamate/` by every build — or
build the library with CMake.

## Highlights

- **Self-describing, unit-safe values** — type family (`uint`/`sint`/`float`/
  `float_fix`/`float_dec`/`utf8`/`bool`), bit-width, numeric base, and unit all
  travel with the value and are validated by the parser.
- **Rich unit system** — 163 named physical units, SI/IEC prefixes, and compound
  expressions (`k~g/(m·s²)`), plus **164 ISO 4217 fiat currencies** and **50
  cryptocurrencies** with minor-unit metadata.
- **First-class structure** — nested structs, multi-dimensional arrays with a
  clean `/`-row syntax, references, and native binary embedding via octet streams
  (no Base64).
- **Streaming C API** and **pure-ctypes Python bindings** (`loads`/`dumps` plus
  event-driven `Reader`/`Writer`), with optional `pint` and `numpy` bridges.
- **`bovnar` CLI** — validate, query by path, pretty-print, convert to/from JSON,
  inspect the event stream, and benchmark.
- **Tooling** — registered media type `text/vnd.bovnar` (+ `.bvnr`), syntax
  highlighters (VS Code, Sublime, Vim, Geany), and a 207-case conformance suite
  with an IUT protocol for verifying third-party implementations.

## Breaking changes (the 1.0 freeze — finalized before this line)

These tightened the format and **cannot change within 1.x**:

- **Currencies require a mandatory `$` sigil** (`$USD`, `$BTC`), so a currency
  code can never collide with a physical-unit symbol.
- **Special floats are bare keywords** — `nan`, `inf`, `ninf` (the previous
  `$`-sigil form was removed).
- **Array elements must be homogeneous** — same kind and dimension; sibling
  sub-arrays must be rectangular. Heterogeneous/ragged data uses a struct.
- **`float_fix` values are range-validated** against the declared Q-format
  (out-of-range is `error_value_out_of_range`).

## Stability contract (spec §17)

- **Frozen at 1.0:** the grammar, type families and annotations, arrays/structs/
  octet-streams/references, the homogeneity rules, and the **error-code values**.
- **Additive in 1.x (non-breaking):** new units/prefixes/currencies, new error
  codes appended after the current maximum, and new optional reader/writer flags.
- **Reserved for 2.0:** anything that could invalidate a 1.x document, change how
  it decodes, renumber an error code, or alter the grammar.

## Documentation

Full docs — tutorial, specification, unit/currency reference, C and Python API,
formal EBNF grammar, FAQ, conformance protocol, and cheat sheet — are available
at **https://www.bovnar.io** (each also downloadable as **PDF**), and in the
[`doc/`](https://github.com/sothis/bovnar/tree/main/doc) directory.

## License

MIT.
