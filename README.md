# Bovnar (BVNR)

**A typed, unit-aware, text–binary serialisation format — and its C99 reference implementation.**

[![Spec version](https://img.shields.io/badge/spec-v1.0-blue)](doc/1_bovnar_spec.md)
[![License: MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![C standard](https://img.shields.io/badge/C-C99-orange)](CMakeLists.txt)

---

## Overview

Bovnar bridges the gap between human-readable formats and machine-precise semantics. Every value in a `.bvnr` document carries its own type family, bit-width, numeric base, and physical unit — without any external schema. Hand the file to anyone and they have everything required to interpret it correctly.

```bovnar
# A self-describing configuration document
.config = {
    .host      = "api.example.com";
    .port      = <uint:16> 443;
    .limits    = {
        .timeout    = <float:64,s> 2.5;
        .max_packet = <uint:64,Mi-B> 16;
    };
};
.acceleration = 70.5 k-m·s⁻²;
.velocity     = <float:64,m/s> 9.81;
.payload      = \x00 … binary stream … \x00;
.matrix       = [1, 2, 3]/[4, 5, 6];
```

---

## Key Features

- **Strong, optional typing** — `uint`, `sint`, `float`, `float_fix`, `float_dec`, `utf8` with explicit bit-width (`8`, `16`, `32`, `64`, …) and numeric base (`_2`, `_16`, `_36`, `_85`, …).
- **First-class physical units** — SI base units, derived SI units, and IEC binary prefixes. Compound units such as `m/s²`, `k-g·m/s²`, and `Gi-B` are written inline; no external schema is needed.
- **Inline unit suffix** — `9.81 m/s` is valid without a full type annotation.
- **Native binary embedding** — Octet streams (`\x00 … \x00`) carry raw bytes without Base64 overhead.
- **Multi-dimensional arrays** — Rows separated by `/`; `[1,2,3]/[4,5,6]` is a native 2D structure.
- **Schema-free yet type-safe** — Omit annotations and get well-defined defaults (`uint:64`, `float:64`, …); add them and the parser validates on the fly.
- **Streaming SAX-style reader** — Incremental parsing from memory, a file descriptor, or a socket via a symmetric `on_unverified` / `on_verified` callback pair.
- **Error recovery** — Optional resync mode skips broken assignments and continues parsing — suitable for log streams and unreliable transports.
- **Python bindings** — Pure-`ctypes`, no compiled extension required. Exposes both a high-level `loads`/`dumps` dict-like API and a low-level event-driven streaming API.
- **Syntax highlighting** — Ready-made grammars for VS Code and Geany.
- **Extensively tested** — Unit tests, socket-pair round-trip tests, fuzz harnesses (reader, writer, DOM, utils), and a benchmark binary.

---

## Format at a Glance

| Construct | Syntax | Example |
|---|---|---|
| Assignment | `.key = value ;` | `.x = 42;` |
| Comment | `# … newline` | `# a remark` |
| Type annotation | `<family:width,unit>` before value | `<uint:32,k-m> 1000` |
| Integer | `[-]digits` | `42`, `-7` |
| Float | `[-]digits[.digits][e[±]digits]` | `3.14`, `1e-6` |
| Special float | `$nan$` `$infinity$` `$-infinity$` | `.x = $nan$;` |
| String | `"…"` with C-style escapes | `"hello\nworld"` |
| Symbol | bare identifier (no quotes) | `true`, `Monday` |
| Reference | `&.path.to.key` | `&.config.host` |
| Array | `[ … ]` rows separated by `/` | `[1,2]/[3,4]` |
| Struct | `{ .key = val; … }` | `{.x = 1; .y = 2;}` |
| Octet stream | `\x00 … binary … \x00` | raw bytes |

---

## Where Bovnar Fits

The serialisation landscape already contains well-established tools, each shaped by a specific set of trade-offs.

JSON and TOML excel as human-writable configuration and API interchange formats. Their strength is ubiquity and tooling saturation. Their weakness is that they carry no semantic type information: a bare `9.81` could be meters per second, volts, or a dimensionless ratio, and the receiving application has no way to know without consulting an external schema, a naming convention, or documentation.

YAML extends JSON's expressiveness with references, block syntax, and multi-document streams, but at the cost of well-known parsing ambiguity. That ambiguity is not a curiosity; it is evidence of a design that prioritises human brevity over machine predictability.

CBOR and MessagePack are binary-first formats optimised for compact wire encoding. They carry type tags and represent binary data efficiently, but the output is opaque: you cannot inspect a CBOR frame with a text editor, and physical units are entirely outside their scope.

Protocol Buffers and FlatBuffers anchor the strongly-typed, schema-driven end of the spectrum. They deliver excellent performance and language-neutral interoperability, but every consumer of the data must have access to the `.proto` or `.fbs` schema file. The data is not self-describing: stripped of its schema, a Protobuf payload is uninterpretable.

Bovnar occupies a different position. It is text-based and human-readable in the same sense that JSON is, but every value is annotated with its type family, bit-width, numeric base, and physical unit — inline, in the byte stream, without any external schema. A `.bvnr` file is self-describing at the individual value level: `<float:64,m/s> 9.81` carries more information than `9.81` can ever carry on its own. The format additionally supports native binary embedding through octet streams, avoiding the size and entropy cost of Base64, and first-class multi-dimensional array syntax that does not reduce to nested lists.

This combination makes Bovnar particularly suited to contexts where the receiving party may not share the sender's schema, where physical correctness matters, or where text readability and binary payloads must coexist in the same document. It is not a replacement for JSON in simple REST APIs, nor for Protobuf in performance-critical RPC. It fills the space where the data itself must carry its own meaning: scientific instrumentation, IoT telemetry, typed configuration files, long-term archival, and mixed text-binary log streams.

If you only need simple key-value interchange, JSON remains the pragmatic choice. If minimal wire size is the overriding constraint, CBOR or Protobuf will outperform any text format. Bovnar is the right tool when precision, self-description, and physical units are requirements rather than nice-to-haves.

---

## Repository Layout

```
bovnar/
├── include/             # Public C headers
│   ├── bovnar.h         # Primary API: reader, writer, events, types
│   ├── bovnar_dom.h     # DOM (tree) API
│   ├── bovnar_si_units.h
│   ├── bvn_float.h
│   └── bvn_int.h
├── src/
│   ├── bovnar.c         # CLI entry point
│   ├── lexer/           # Tokeniser + state table
│   ├── validator/       # Semantic validation layer
│   ├── writer/          # Serialiser
│   ├── io/              # FD / memory source & sink
│   ├── dom/             # DOM builder and traversal
│   └── utils/           # SI units, integer, float utilities
├── tests/               # C unit, integration, fuzz, and benchmark tests
├── python/
│   └── bovnar/          # Pure-ctypes Python bindings
│       ├── __init__.py  # loads / dumps / Reader / Writer
│       ├── reader.py
│       ├── writer.py
│       ├── dom.py
│       ├── units.py
│       ├── structs.py
│       ├── enums.py
│       ├── exceptions.py
│       └── _ffi.py
├── examples/            # Annotated .bvnr example files
├── highlighter/
│   ├── vscode/          # VS Code TextMate grammar
│   └── geany/           # Geany filetype definition
├── doc/
│   ├── 0_bovnar_tutorial.md
│   ├── 1_bovnar_spec.md            # Format specification v1.0
│   ├── 2_bovnar_unit_system.md
│   ├── 3_bovnar_readwrite_api.md
│   ├── 4_bovnar_python_bindings.md
│   ├── 5_bovnar.ebnf               # Formal EBNF grammar
│   └── 6_bovnar_faq.md             # Frequently asked questions
├── CMakeLists.txt
└── CMakeLists_tests.txt
```

---

## Building

### Requirements

- CMake ≥ 3.21
- A C99-conforming compiler (GCC or Clang recommended)
- `libm` (math library, standard on all POSIX systems)
- `libgmp` — optional; enables the arbitrary-precision integer cross-check test binary (`bvnr_int_test`). If absent, that single test is automatically skipped and the rest of the build proceeds normally.

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

Build types: `Debug` (`-O0 -g3`), `Release` (`-O2`), `MinSizeRel` (`-Os`), `RelWithDebInfo` (`-O3 -g3`).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build
```

### Link against the library

```bash
gcc my_app.c -I include -L build -lbvnr_static -lm -o my_app
```

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

| Binary | Coverage |
|---|---|
| `bvnr_reader_test` | Core reader, all token types |
| `bvnr_extended_reader_test` | Edge cases, resync, error recovery |
| `bvnr_writer_test` | Serialiser output |
| `bvnr_socketpair_roundtrip_test` | Full round-trip over a POSIX socketpair |
| `bvnr_dom_test` | DOM builder and traversal |
| `bvnr_si_test` | SI/IEC unit parsing and formatting |
| `bvnr_unit_ext_test` | Extended unit symbols, long-name aliases, prefix enforcement |
| `bvnr_utils_test` | Utility functions |
| `bvnr_int_test` | Arbitrary-precision integer (optional; requires libgmp — auto-skipped if absent) |
| `bvnr_float_test` | Floating-point representation |
| `bvnr_float_fix_dec_test` | Fixed and decimal float modes |
| `bvnr_high_severity_test` | Robustness under malformed input |
| `bvnr_fuzz_test --harness reader\|dom\|utils` | Randomised fuzzing of reader, DOM, and utils |
| `bvnr_fuzz_writer_test` | Randomised fuzzing of the serialiser |

### Python tests

```bash
cmake -B build -DBVNR_BUILD_PYTHON_TESTS=ON .
cmake --build build
cd build && ctest -L python --output-on-failure
```

Or directly:

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

See [doc/4_bovnar_python_bindings.md](doc/4_bovnar_python_bindings.md) for the full API.

---

## Syntax Highlighting

### VS Code

```bash
cd highlighter/vscode && ./install.sh
```

### Geany

```bash
cd highlighter/geany && ./install.sh
```

---

## Use Cases

- **Scientific and measurement data** — numbers with explicit physical units travel with the payload.
- **Embedded systems / IoT** — the streaming parser has a small, allocation-friendly footprint; no heap required for the lexer itself.
- **Hardware and software configuration** — typed values eliminate range ambiguity (e.g., `<uint:16>` for a port number).
- **Mixed text + binary payloads** — log entries with attached raw memory dumps, firmware images, or sensor frames.
- **Multi-dimensional data** — matrices, image frames, time-series batches, any tabular structure where rows are a natural unit.
- **Long-term archival** — self-describing data stays interpretable without an out-of-band schema.

---

## Documentation

| Document | Description |
|---|---|
| [Specification v1.0](doc/1_bovnar_spec.md) | Full lexical and syntactic grammar, type system, arrays, structs, octet streams, validation rules, and formal EBNF. |
| [Tutorial](doc/0_bovnar_tutorial.md) | Practical introduction for developers familiar with JSON or similar formats. |
| [Unit System Reference](doc/2_bovnar_unit_system.md) | SI and IEC prefixes, base units, compound units, exponents, C API, and validation rules. |
| [Read & Write API](doc/3_bovnar_readwrite_api.md) | Complete C API for streaming readers and writers with annotated examples. |
| [Python Bindings](doc/4_bovnar_python_bindings.md) | Pure-ctypes Python interface: high-level `loads`/`dumps`, streaming `Reader`/`Writer`, unit helpers. |
| [Formal EBNF](doc/5_bovnar.ebnf) | Machine-readable grammar. |
| [FAQ](doc/6_bovnar_faq.md) | Frequently asked questions covering the format, type system, units, C API, Python bindings, and limits. |

---

## License

| Component | License |
|---|---|
| Source code (C, Python, CMake) | [MIT](LICENSE) |
| Documentation (`doc/`) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Examples (`examples/`) | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |

Copyright © 2026 Janos Sonntag (born Laube).
