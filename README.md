# Bovnar (BVNR)

**Unit-safe serialization for scientific and industrial systems — with a C99 reference implementation.**

[![Spec version](https://img.shields.io/badge/spec-1.0%20stable-blue)](doc/1_bovnar_spec.md)
[![License: MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![C standard](https://img.shields.io/badge/C-C99-orange)](CMakeLists.txt)

---

## Overview

In scientific and industrial systems, the expensive failures are rarely bad syntax — they are unit confusion: a value sent in pounds-force and read as newtons, feet read as meters. The number parses fine; the dimension is wrong.

Bovnar closes that gap. Every value in a `.bvnr` document carries its own type family, bit-width, numeric base, and **physical unit** — inline, in the byte stream, with no external schema. The unit is not a comment or a naming convention; it is part of the value and is validated by the parser. Annotate a measurement as `m/s` and write a mismatched inline unit, and parsing fails with `error_unit_mismatch`. Hand the file to anyone and they have everything required to interpret — and to dimensionally trust — every reading.

```bovnar
# A self-describing configuration document
.config = {
    .host      = "api.example.com";
    .port      = <uint:16> 443;
    .limits    = {
        .timeout    = <float:64,s> 2.5;
        .max_packet = <uint:64,Mi~B> 16;
    };
};
.acceleration = 70.5 k~m·s⁻²;
.velocity     = <float:64,m/s> 9.81;
.price        = <float_dec:64,$USD> 19.99;
.btc_balance  = <uint:64,$BTC> 547820000;   # satoshis
.payload      = \x00 … binary stream … \x00;
.matrix       = [1, 2, 3]/[4, 5, 6];
```

---

## Key Features

- **Strong, optional typing** — seven families (`uint`, `sint`, `float`, `float_fix`, `float_dec`, `utf8`, `bool`) with explicit bit-width (`8`, `16`, `32`, `64`, …) and numeric base (`_2`, `_16`, `_36`, `_85`, …).
- **First-class physical units** — SI base units, derived SI units, and IEC binary prefixes. Compound units such as `m/s²`, `k~g·m/s²`, and `Gi~B` are written inline; no external schema is needed.
- **Currency units** — 164 ISO 4217 fiat currencies and 50 cryptocurrencies are first-class units, written with a mandatory `$` sigil (`<float_dec:64,$USD> 19.99`, `<uint:64,$BTC> 547820000`), each carrying minor-unit metadata and prefix-validity rules.
- **Inline unit suffix** — `9.81 m/s` is valid without a full type annotation.
- **Native binary embedding** — Octet streams (`\x00 … \x00`) carry raw bytes without Base64 overhead.
- **Multi-dimensional arrays** — Rows separated by `/`; `[1,2,3]/[4,5,6]` is a native 2D structure.
- **Schema-free yet type-safe** — Omit annotations and get well-defined defaults (`uint:64`, `float:64`, …); add them and the parser validates on the fly.
- **Streaming SAX-style reader** — Incremental parsing from memory, a file descriptor, or a socket via a symmetric `on_unverified` / `on_verified` callback pair.
- **Error recovery** — Optional resync mode skips broken assignments and continues parsing — suitable for log streams and unreliable transports.
- **Python bindings** — Pure-`ctypes`, no compiled extension required. Exposes both a high-level `loads`/`dumps` dict-like API and a low-level event-driven streaming API.
- **Command-line tool** — `bovnar` validates, queries values by path, pretty-prints, converts to and from JSON, dumps the lexer/validator event stream, and benchmarks parsing throughput.
- **Browser playground** — a dependency-free JavaScript parser (`bovnar_parser.js`) approximates the C reference event stream (lenient: it does not synthesise default type annotations or perform type/value validation) and powers an interactive single-file web playground.
- **Syntax highlighting** — Ready-made grammars for VS Code, Sublime Text, Geany, and Vim, all sharing one "cyberpunk" colour scheme with depth-cycling brackets.
- **Extensively tested** — Unit tests, socket-pair round-trip tests, a 202-case conformance suite, fuzz harnesses (reader, writer, DOM, utils), and a built-in benchmark mode (`bovnar bench`).

---

## Format at a Glance

| Construct | Syntax | Example |
|---|---|---|
| Assignment | `.key = value ;` | `.x = 42;` |
| Comment | `# … newline` | `# a remark` |
| Type annotation | `<family:width,unit>` before value | `<uint:32,k~m> 1000` |
| Integer | `[-]digits` | `42`, `-7` |
| Float | `[-]digits[.digits][e[±]digits]` | `3.14`, `1e-6` |
| Special float | `nan` `inf` `ninf` | `.x = nan;` |
| Boolean | `true` `false` `on` `off` (`<bool>`) | `.b = on;` |
| String | `"…"` with C-style escapes | `"hello\nworld"` |
| Symbol | bare identifier (no quotes) | `ok`, `Monday` |
| Reference | `&.path.to.key` | `&.config.host` |
| Array | `[ … ]` rows separated by `/` | `[1,2]/[3,4]` |
| Struct | `{ .key = val; … }` | `{.x = 1; .y = 2;}` |
| Null | empty slot or `null` keyword | `.x = ;`, `.x = null;` |
| Octet stream | `\x00 … binary … \x00` | raw bytes |

---

## Where Bovnar Fits

The serialisation landscape already contains well-established tools, each shaped by a specific set of trade-offs.

JSON and TOML excel as human-writable configuration and API interchange formats. Their strength is ubiquity and tooling saturation. Their weakness is that they carry no semantic type information: a bare `9.81` could be meters per second, volts, or a dimensionless ratio, and the receiving application has no way to know without consulting an external schema, a naming convention, or documentation.

YAML extends JSON's expressiveness with references, block syntax, and multi-document streams, but at the cost of well-known parsing ambiguity. That ambiguity is not a curiosity; it is evidence of a design that prioritises human brevity over machine predictability.

CBOR and MessagePack are binary-first formats optimised for compact wire encoding. They carry type tags and represent binary data efficiently, but the output is opaque: you cannot inspect a CBOR frame with a text editor, and physical units are entirely outside their scope.

Protocol Buffers and FlatBuffers anchor the strongly-typed, schema-driven end of the spectrum. They deliver excellent performance and language-neutral interoperability, but every consumer of the data must have access to the `.proto` or `.fbs` schema file. The data is not self-describing: stripped of its schema, a Protobuf payload is uninterpretable.

Bovnar occupies a different position. It is text-based and human-readable in the same sense that JSON is, but every value is annotated with its type family, bit-width, numeric base, and physical unit — inline, in the byte stream, without any external schema. A `.bvnr` file is self-describing at the individual value level: `<float:64,m/s> 9.81` carries more information than `9.81` can ever carry on its own. The format additionally supports native binary embedding through octet streams, avoiding the size and entropy cost of Base64, and first-class multi-dimensional array syntax that does not reduce to nested lists.

This combination makes Bovnar particularly suited to contexts where dimensional correctness matters, where the receiving party may not share the sender's schema, or where text readability and binary payloads must coexist in the same document. It is not a replacement for JSON in simple REST APIs, nor for Protobuf in performance-critical RPC. It is built for the place where a wrong unit is a failure: scientific instrumentation and metrology, industrial telemetry and control, IoT sensor networks, long-term measurement archival, and mixed text-binary log streams.

If you only need simple key-value interchange, JSON remains the pragmatic choice. If minimal wire size is the overriding constraint, CBOR or Protobuf will outperform any text format. Bovnar is the right tool when unit-safety, precision, and self-description are requirements rather than nice-to-haves.

---

## Repository Layout

```
bovnar/
├── include/                 # Public C headers
│   ├── bovnar.h             # Primary API: reader, writer, events, types
│   ├── bovnar_dom.h         # DOM (tree) API
│   ├── bovnar_si_units.h    # SI / IEC unit API
│   ├── bovnar_currency.h    # Fiat + crypto currency API
│   ├── bvn_float.h
│   └── bvn_int.h
├── src/
│   ├── bovnar.c             # CLI entry point
│   ├── lexer/               # Tokeniser + state table
│   ├── validator/           # Semantic validation layer
│   ├── writer/              # Serialiser + canonicalising observer
│   ├── io/                  # FD / memory source & sink
│   ├── dom/                 # DOM builder and traversal
│   └── utils/               # SI units, currency, integer, float utilities
├── tests/                   # C unit, integration, conformance, and fuzz tests
├── python/
│   └── bovnar/              # Pure-ctypes Python bindings
│       ├── __init__.py      # loads / dumps / dom_parse / Reader / Writer
│       ├── reader.py
│       ├── writer.py
│       ├── dom.py
│       ├── units.py
│       ├── currency.py
│       ├── quantity.py
│       ├── structs.py
│       ├── enums.py
│       ├── exceptions.py
│       └── _ffi.py
├── examples/                # Annotated .bvnr example files
├── highlighter/
│   ├── vscode/              # VS Code TextMate grammar + theme
│   ├── sublime/             # Sublime Text syntax + colour scheme
│   ├── geany/               # Geany filetype definition
│   └── vim/                 # Vim syntax + filetype plugin
├── web/                     # Single-file browser playground
│   ├── index.html           # Playground + landing page
│   └── bovnar_parser.js     # Dependency-free JavaScript parser
├── doc/
│   ├── 0_bovnar_tutorial.md
│   ├── 1_bovnar_spec.md            # Format specification (v1.0, stable)
│   ├── 2_bovnar_unit_system.md
│   ├── 3_bovnar_readwrite_api.md
│   ├── 4_bovnar_python_bindings.md
│   ├── 5_bovnar.ebnf               # Formal EBNF grammar
│   ├── 6_bovnar_faq.md             # Frequently asked questions
│   ├── 7_bovnar_conformance.md     # Conformance test tool and IUT protocol
│   └── 8_unit_cheatsheet.md        # Units & currencies quick reference
├── CMakeLists.txt
└── CMakeLists_tests.txt
```

---

## Building

### Requirements

- CMake ≥ 3.21
- A C99-conforming compiler (GCC or Clang recommended)
- `libm` (math library, standard on all POSIX systems)
- `libgmp` — optional; enables the arbitrary-precision integer cross-check sections of `bvnr_int_test`. If absent, that test still builds and runs — only the GMP comparison sections are skipped.

### Build the library and CLI tool

```bash
cmake -B build .
cmake --build build
```

This produces:

| Target | Path |
|---|---|
| Static library | `build/libbvnr_static.a` |
| Shared library | `build/libbvnr_shared.so` |
| CLI binary | `build/bovnar` |

Build types: `Debug` (`-O0 -g3`), `Release` (`-O3 -flto`), `MinSizeRel` (`-Os`), `RelWithDebInfo` (`-O3 -g3 -flto`). `Release` and `RelWithDebInfo` enable link-time optimisation.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build
```

### Build options

| Option | Default | Effect |
|---|---|---|
| `BVNR_HARDEN` | `ON` | Stack protector (CLI), `_FORTIFY_SOURCE=2`, and `-Wformat` security checks. |
| `BVNR_WERROR` | `OFF` | Promote all warnings to errors (for CI). |
| `BVNR_FUZZ_TEST` | `ON` | Build the self-contained fuzz harnesses and register them as CTest tests. |
| `BVNR_FUZZ_EXTERNAL` | `OFF` | Build libFuzzer / AFL++ targets (requires clang or afl-clang-fast). |

```bash
cmake -B build -DBVNR_WERROR=ON .
```

### Link against the library

```bash
gcc my_app.c -I include -L build -lbvnr_static -lm -o my_app
```

---

## Command-Line Tool

The `bovnar` binary built above wraps the library for everyday use:

| Command | Description |
|---|---|
| `bovnar validate <file>` | Validate a `.bvnr` file; exit non-zero on the first error. |
| `bovnar query <path> <file>` | Print a single value by dotted path, e.g. `.sensor.temperature`. |
| `bovnar pretty-print <file>` | Re-serialise a document in canonical pretty form. |
| `bovnar convert <file>` | Convert between `json` and `bvnr`; direction is auto-detected from the `.json`/`.bvnr` extension. Add `--from <fmt> --to <fmt>` to override. |
| `bovnar events [-c] [-d] [-p] <file\|->` | Print the lexer (unverified) and validator (verified) event streams side by side. `-c` resync on error, `-d` debug re-serialisation, `-p` pretty debug output. Pass `-` to read stdin. |
| `bovnar bench [options]` | Benchmark parsing throughput across profiles and payload sizes; `--json` for machine-readable output. |

```bash
bovnar validate examples/units.bvnr
bovnar query .system.host config.bvnr
bovnar convert data.json          # json -> bvnr (direction from extension)
bovnar convert data.bvnr          # bvnr -> json
cat data.bvnr | bovnar events -
```

### JSON conversion: what is and isn't preserved

The two data models do not fully overlap, so conversion is faithful in one
direction and necessarily lossy in the other. The converter never silently
drops or corrupts data — anything it cannot represent is a hard error with a
diagnostic, not a quietly mangled result.

**`json → bvnr`** is value-preserving for the data bovnar can represent. Integers
keep full 64-bit range in both signs (including unsigned values above
`INT64_MAX`); homogeneous JSON arrays — flat lists, rectangular nested arrays,
arrays of same-shaped objects — map to bovnar arrays element-for-element, and
nested objects become structs. It rejects, rather than mauls:

- a top-level value that is not an object (bovnar documents are sets of
  assignments);
- object keys that are not valid bovnar identifiers (spaces, leading digits,
  punctuation, empty);
- integer literals that exceed 64 bits, and malformed JSON (leading zeros,
  invalid escapes, unescaped control characters, lone surrogates, trailing
  content);
- nesting deeper than the writer's array/struct limit (errors cleanly);
- **heterogeneous or ragged arrays** — bovnar 1.0 arrays are homogeneous (see the
  spec's §7.4), so a JSON array that mixes element kinds (`[1, "two", {…}]`) or
  whose sub-arrays differ in length (`[[1,2],[3,4,5]]`) has no bovnar
  representation and is a hard error. Model such data as an object/struct.

One representational caveat: bovnar has no zero-length array (`[]` denotes a
single null element), so an empty JSON array round-trips to `[null]`.

**`bvnr → json`** keeps every value but cannot carry bovnar's extra semantics,
since JSON has no equivalent:

- type annotations — bit-width, base, and **physical unit / currency** — are
  dropped (a `<float:64,m/s> 9.81` becomes a bare `9.81`);
- symbols and references are emitted as strings; octet streams as a lowercase
  hex string;
- integers wider than 64 bits are emitted as decimal strings (JSON cannot hold
  them as numbers safely);
- `nan` and `inf` become `null` (JSON has no non-finite numbers).

Consequently `json → bvnr → json` preserves every value (the empty-array case
above aside). Integers, booleans, null, strings, and array/object structure also
reproduce *textually*; floating-point values reproduce their exact `double`
value but not necessarily their original spelling. Floats are re-emitted as the
**shortest decimal that round-trips** to the same `double`, so `0.1` comes back
as `0.1` — but the lexical form is lost when the literal is parsed, so `2.0`
comes back as `2` and `1e10` as `1e+10`. The reverse path, `bvnr → json → bvnr`,
is lossy whenever the document uses units, symbols, references, octets, or wide
integers.

---

## Running the Tests

```bash
cmake -B build .
cmake --build build
cd build && ctest --output-on-failure
```

Or use the convenience wrapper at the repository root:

```bash
./run_tests.sh
```

### Test suite

| Binary / test | Coverage |
|---|---|
| `bvnr_reader_test` | Core reader, all token types |
| `bvnr_extended_reader_test` | Edge cases, resync, error recovery |
| `bvnr_writer_test` | Serialiser output |
| `bvnr_socketpair_roundtrip_test` | Full round-trip over a POSIX socketpair |
| `bvnr_dom_test` | DOM builder and traversal |
| `bvnr_si_test` | SI/IEC unit parsing and formatting |
| `bvnr_unit_ext_test` | Extended unit symbols, long-name aliases, prefix enforcement |
| `bvnr_currency_test` | Fiat and crypto currency lookup, minor units, prefix rules |
| `bvnr_utils_test` | Utility functions |
| `bvnr_int_test` | Arbitrary-precision integer arithmetic (GMP cross-check sections skipped when libgmp is absent) |
| `bvnr_float_test` | Floating-point representation |
| `bvnr_float_fix_dec_test` | Fixed and decimal float modes |
| `bvnr_high_severity_test` | Robustness under malformed input |
| `bvnr_conformance` | 202-case conformance suite — self-test plus `--iut` adapter mode |
| `bvnr_fuzz_test --harness reader\|dom\|utils` | Randomised fuzzing of reader, DOM, and utils |
| `bvnr_fuzz_writer_test` | Randomised fuzzing of the serialiser |

CTest additionally runs every file in `examples/` through `bovnar events` and `bovnar validate` as smoke tests. Label filters narrow the run, e.g. `ctest -L unit`, `ctest -L conformance`, `ctest -L fuzz_deep`, or `ctest -L cli`.

### Python tests

Python binding tests are registered automatically when a Python 3 interpreter is found at configure time — no extra flag is required:

```bash
cmake -B build .
cmake --build build
cd build && ctest -L python --output-on-failure
```

Pure-Python tests (`ctest -L python_pure`) run without the shared library; integration tests (`ctest -L python_integration`) need `libbvnr_shared.so`, whose path CTest injects automatically. To run pytest directly instead:

```bash
export LIBBOVNAR_PATH=$(pwd)/build/libbvnr_shared.so
cd python && pip install -e ".[dev]"
pytest tests -v
```

---

## C API — Quick Start

### Reading from memory

Callbacks are registered through `bvnr_read_flags_t`. Both `on_unverified` and `on_verified` receive `(void *userdata, bvnr_event_t ev, bvnr_data_t *d)` and must return `true` to continue parsing.

```c
#include "bovnar.h"

static bool on_event(void *userdata, bvnr_event_t ev, bvnr_data_t *d)
{
    (void)userdata;
    if (ev == ev_data)
        printf("key=%.*s\n", (int)d->length, (const char *)d->data);
    return true;
}

int main(void)
{
    const char *src = ".velocity = <float:64,m/s> 9.81;";
    bvnr_read_flags_t opts = {0};
    opts.on_verified = on_event;
    bvnr_reader_t *r = bvnr_reader_create();
    bvnr_open_read_mem(r, src, (uint64_t)strlen(src), NULL, 0, &opts);
    bvnr_read(r);
    bvnr_reader_destroy(r);
}
```

### Reading from a file descriptor

```c
bvnr_source_t src;
bvnr_source_from_fd(&src, fd);
bvnr_read_flags_t opts = {0};
opts.on_verified = on_event;
bvnr_open_read_source(r, &src, NULL, &opts);
bvnr_read(r);
```

### Writing

The high-level writer helpers accept a key string and a typed value directly:

```c
#include "bovnar.h"

int main(void)
{
    char buf[256];
    bvnr_writer_t *w = bvnr_writer_create();
    bvnr_sink_t sink;
    bvnr_sink_to_mem(&sink, (uint8_t *)buf, sizeof(buf));
    bvnr_open_write_sink(w, &sink, true, NULL);

    bvnr_write_float(w, "velocity", 64, 9.81);

    bvnr_write_finish(w);
    bvnr_writer_destroy(w);
    puts(buf);
}
```

See [doc/3_bovnar_readwrite_api.md](doc/3_bovnar_readwrite_api.md) for the complete API reference, including the low-level `bvnr_write_event` interface and the full set of typed write helpers (`bvnr_write_uint`, `bvnr_write_sint`, `bvnr_write_float_unit`, etc.).

---

## Python Bindings — Quick Start

### Requirements

- **Python ≥ 3.10**
- The shared library `libbvnr_shared.so` (built as shown above)

### Installation

```bash
cmake -B build . && cmake --build build

export LIBBOVNAR_PATH=$(pwd)/build/libbvnr_shared.so

cd python
pip install -e ".[dev]"
```

### High-level API

```python
import bovnar

# Serialise
data = {"sensor_id": 42, "temperature": 36.6, "unit": "celsius"}
raw = bovnar.dumps(data)

# Deserialise
doc = bovnar.loads(raw)
print(doc["sensor_id"])      # 42
print(doc["temperature"])    # 36.6
```

### Streaming event-driven API

```python
from bovnar import Reader, Event

def on_event(ev, data):
    if ev == Event.DATA and data.value_unit.num_components:
        print(data.raw_str(), bovnar.unit_to_str(data.value_unit))

Reader().read_mem(b".velocity = <float:64,m/s> 9.81;", on_verified=on_event)
# → 9.81  m/s
```

Beyond `loads`/`dumps`, the package provides `dom_parse()` for random-access tree traversal, `loads(..., typed=True)` to preserve each value's exact type and unit as `Quantity` objects for lossless round-trips, and a `currency` module mirroring the C currency API.

See [doc/4_bovnar_python_bindings.md](doc/4_bovnar_python_bindings.md) for the full API.

---

## Syntax Highlighting

### VS Code

```bash
cd highlighter/vscode && ./install.sh
```

### Sublime Text

```bash
cd highlighter/sublime && ./install.sh
```

Then choose **Preferences → Select Color Scheme… → Bovnar Cyberpunk**.

### Geany

```bash
cd highlighter/geany && ./install.sh
```

### Vim

```bash
cd highlighter/vim && ./install.sh
```

---

## Web Playground

A dependency-free JavaScript parser (`web/bovnar_parser.js`) approximates the reference event stream in the browser and drives an interactive single-file playground (`web/index.html`). It is deliberately lenient — it adds an `ev_assignment_end` delimiter the C core does not emit, does not synthesise the default type-annotation events, and performs no type/value validation — so it is a visualisation aid, not a conformant second implementation. Serve the `web/` directory and open it — no build step required:

```bash
cd web && ./httpd.sh          # python3 -m http.server
# then open http://localhost:8000/
```

---

## Use Cases

- **Scientific and measurement data** — numbers with explicit physical units travel with the payload.
- **Financial and crypto data** — monetary amounts carry their ISO 4217 or cryptocurrency unit and minor-unit precision inline, removing ambiguity between currencies and scales.
- **Embedded systems / IoT** — the streaming parser has a small, allocation-friendly footprint; no heap required for the lexer itself.
- **Hardware and software configuration** — typed values eliminate range ambiguity (e.g., `<uint:16>` for a port number).
- **Mixed text + binary payloads** — log entries with attached raw memory dumps, firmware images, or sensor frames.
- **Multi-dimensional data** — matrices, image frames, time-series batches, any tabular structure where rows are a natural unit.
- **Long-term archival** — self-describing data stays interpretable without an out-of-band schema.

---

## Documentation

| Document | Description |
|---|---|
| [Specification (v1.0, stable)](doc/1_bovnar_spec.md) | Full lexical and syntactic grammar, type system, arrays, structs, octet streams, validation rules, and formal EBNF. |
| [Tutorial](doc/0_bovnar_tutorial.md) | Practical introduction for developers familiar with JSON or similar formats. |
| [Unit System Reference](doc/2_bovnar_unit_system.md) | SI and IEC prefixes, base units, compound units, exponents, C API, and validation rules. |
| [Read & Write API](doc/3_bovnar_readwrite_api.md) | Complete C API for streaming readers and writers with annotated examples. |
| [Python Bindings](doc/4_bovnar_python_bindings.md) | Pure-ctypes Python interface: high-level `loads`/`dumps`, streaming `Reader`/`Writer`, unit helpers. |
| [Formal EBNF](doc/5_bovnar.ebnf) | Machine-readable grammar. |
| [FAQ](doc/6_bovnar_faq.md) | Frequently asked questions covering the format, type system, units, C API, Python bindings, and limits. |
| [Conformance Test Tool](doc/7_bovnar_conformance.md) | Conformance suite (202 cases), IUT protocol for verifying third-party implementations, TAP output, and CTest integration. |
| [Units & Currencies Cheat Sheet](doc/8_unit_cheatsheet.md) | Quick reference for every physical unit, 164 fiat currencies, and 50 cryptocurrencies, with prefix tables and symbol-disambiguation rules. |

---

## License

| Component | License |
|---|---|
| Source code (C, Python, CMake) | [MIT](LICENSE) |
| Documentation (`doc/`) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Examples (`examples/`) | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |

Copyright © 2026 Janos Sonntag.
