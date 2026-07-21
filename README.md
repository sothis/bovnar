# Bovnar (BVNR)

**Unit-safe serialization for scientific and industrial systems — with a C99 reference implementation.**

[![Spec version](https://img.shields.io/badge/spec-1.1-blue)](doc/1_bovnar_spec.md)
[![License: MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![C standard](https://img.shields.io/badge/C-C99-orange)](CMakeLists.txt)

---

## Overview

In scientific and industrial systems, the expensive failures are rarely bad syntax — they are unit confusion: a value sent in pounds-force and read as newtons, feet read as meters. The number parses fine; the dimension is wrong.

Bovnar closes that gap. Every value in a `.bvnr` document carries its own type family, bit-width, numeric base, and **physical unit** — inline, in the byte stream, with no external schema. The unit is not a comment or a naming convention; it is part of the value and is validated by the parser. Annotate a measurement as `m/s` and write a mismatched inline unit, and parsing fails with `error_unit_mismatch`. Hand the file to anyone and they have everything required to interpret — and to dimensionally trust — every reading.

```bovnar
#!bovnar 1.1                                 # optional spec-version directive
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
.created      = <datetime:64,unix> 1750000000;   # a timestamp (spec 1.1)
.payload      = \x00 … binary stream … \x00;
.matrix       = [1, 2, 3]/[4, 5, 6];
.cell         = &.matrix[0][1];             # reference indexing (spec 1.1) → 2
```

---

## Key Features

- **Strong, optional typing** — seven core families (`uint`, `sint`, `float`, `float_fix`, `float_dec`, `utf8`, `bool`), plus `datetime` (spec 1.1), with explicit bit-width (`8`, `16`, `32`, `64`, …) and numeric base (`_2`, `_16`, `_36`, `_85`, …).
- **First-class physical units** — SI base units, derived SI units, and IEC binary prefixes. Compound units such as `m/s²`, `k~g·m/s²`, and `Gi~B` are written inline; no external schema is needed.
- **Currency units** — 166 ISO 4217 fiat currencies and 50 cryptocurrencies are first-class units, written with a mandatory `$` sigil (`<float_dec:64,$USD> 19.99`, `<uint:64,$BTC> 547820000`), each carrying minor-unit metadata and prefix-validity rules.
- **Inline unit suffix** — `9.81 m/s` is valid without a full type annotation.
- **Native binary embedding** — Octet streams (`\x00 … \x00`) carry raw bytes without Base64 overhead.
- **Multi-dimensional arrays** — Rows separated by `/`; `[1,2,3]/[4,5,6]` is a native 2D structure.
- **Schema-free yet type-safe** — Omit annotations and get well-defined defaults (`uint:64`, `float:64`, …); add them and the parser validates on the fly.
- **Streaming SAX-style reader** — Incremental parsing from memory, a file descriptor, or a socket via a symmetric `on_unverified` / `on_verified` callback pair.
- **Error recovery** — Optional resync mode skips broken assignments and continues parsing — suitable for log streams and unreliable transports.
- **Python bindings** — Pure-`ctypes`, no compiled extension required. Exposes both a high-level `loads`/`dumps` dict-like API and a low-level event-driven streaming API.
- **Command-line tool** — `bovnar` validates, queries values by path, pretty-prints, converts to and from JSON, dumps the lexer/validator event stream, and benchmarks parsing throughput.
- **Browser playground** — a dependency-free JavaScript parser (`bovnar_parser.js`) approximates the C reference event stream (lenient: it does not synthesise default type annotations or perform type/value validation) and powers an interactive single-file web playground.
- **Syntax highlighting** — Ready-made grammars for VS Code, Sublime Text, Geany, Vim, and CLion (JetBrains), all sharing one "cyberpunk" colour scheme with depth-cycling brackets.
- **Extensively tested** — Unit tests, socket-pair round-trip tests, a 319-case conformance suite, fuzz harnesses (reader, writer, DOM, utils), and a built-in benchmark mode (`bovnar bench`).

---

## Format at a Glance

| Construct | Syntax | Example |
|---|---|---|
| Assignment | `.key = value ;` | `.x = 42;` |
| Comment | `# … newline` | `# a remark` |
| Type annotation | `<family:width,base,unit>` before value | `<uint:32,_10,k~m> 1000` |
| Integer | `[-]digits` | `42`, `-7` |
| Float | `[-]digits[.digits][e[±]digits]` | `3.14`, `1e-6` |
| Special float | `nan` `inf` `ninf` | `.x = nan;` |
| Boolean | `true` `false` `on` `off` (`<bool>`) | `.b = on;` |
| String | `"…"` with C-style escapes (`\u{…}`/`\xHH` in spec 1.1) | `"hello\nworld"`, `"caf\u{e9}"` |
| Symbol | bare identifier (no quotes) | `ok`, `Monday` |
| Reference | `&.path.to.key`; array indexing `&.path[i][j]` (spec 1.1) | `&.config.host`, `&.matrix[0][1]` |
| Array | `[ … ]` rows separated by `/` | `[1,2]/[3,4]` |
| Struct | `{ .key = val; … }` | `{.x = 1; .y = 2;}` |
| Null | empty slot or `null` keyword | `.x = ;`, `.x = null;` |
| Octet stream | `\x00 … binary … \x00` | raw bytes |
| Version directive (spec 1.1) | `#!bovnar <major>.<minor>` (first line) | `#!bovnar 1.1` |
| Datetime (spec 1.1) | `<datetime:width,epoch>` signed epoch seconds | `<datetime:64,unix> 1750000000` |
| Datetime literal (spec 1.1) | ISO-8601 (`Z` or `±HH:MM` offset, optional fraction); bare form infers `<datetime:64,unix>` | `2026-06-15T12:00:00+02:00` |

---

## Where Bovnar Fits

Bovnar is built for one thing: data where the **meaning must travel with the value**. Every value is annotated with its type family, bit-width, numeric base, and physical unit — inline, in the byte stream, without any external schema. A `.bvnr` file is self-describing at the individual value level: `<float:64,m/s> 9.81` carries more information than `9.81` can ever carry on its own. The format additionally supports native binary embedding through octet streams, avoiding the size and entropy cost of Base64, and first-class multi-dimensional array syntax that does not reduce to nested lists.

This makes Bovnar suited to contexts where dimensional correctness matters, where the receiving party may not share the sender's schema, or where text readability and binary payloads must coexist in the same document. It is built for the place where a wrong unit is a failure: scientific instrumentation and metrology, industrial telemetry and control, IoT sensor networks, long-term measurement archival, and mixed text-binary log streams.

Reach for Bovnar when unit-safety, precision, and self-description are requirements rather than nice-to-haves — when a value must mean exactly the same thing to its writer, its reader, and an archive opened decades later.

---

## Repository Layout

```
bovnar/
├── include/                 # Public C headers
│   ├── bovnar.h             # Primary API: reader, writer, events, types
│   ├── bovnar_dom.h         # DOM (tree) API
│   ├── bovnar_si_units.h    # SI / IEC unit API
│   ├── bovnar_currency.h    # Fiat + crypto currency API
│   ├── bovnar_stream.h      # Framing, multiplexing & document-in-document
│   ├── bvn_datetime.h       # Datetime / epoch helpers (spec 1.1)
│   ├── bvn_gregorian_date.h # Gregorian calendar conversions (spec 1.1)
│   ├── bvn_float.h
│   └── bvn_int.h
├── src/
│   ├── bovnar.c             # CLI entry point
│   ├── lexer/               # Tokeniser + state table
│   ├── validator/           # Semantic validation layer
│   ├── writer/              # Serialiser + canonicalising observer
│   ├── io/                  # FD / memory source & sink
│   ├── dom/                 # DOM builder and traversal
│   ├── stream/              # Framing / multiplexing on the event API
│   └── utils/               # SI units, currency, datetime, integer, float utilities
├── tests/                   # C unit, integration, conformance, and fuzz tests
├── python/
│   └── bovnar/              # Pure-ctypes Python bindings
│       ├── __init__.py      # loads / dumps / dom_parse / Reader / Writer
│       ├── reader.py
│       ├── writer.py
│       ├── dom.py
│       ├── stream.py        # Framing / multiplexing streaming API
│       ├── units.py
│       ├── currency.py
│       ├── quantity.py
│       ├── structs.py
│       ├── enums.py
│       ├── exceptions.py
│       ├── _ffi.py          # ctypes FFI layer
│       ├── _bvnfloat.py     # float16 / decimal-float helpers
│       └── _numpy.py, _pint_bridge.py, _pint_units.py   # optional NumPy / Pint bridges
├── examples/                # Annotated .bvnr example files
├── highlighter/
│   ├── vscode/              # VS Code TextMate grammar + theme
│   ├── sublime/             # Sublime Text syntax + colour scheme
│   ├── geany/               # Geany filetype definition
│   ├── vim/                 # Vim syntax + filetype plugin
│   └── clion/               # CLion / JetBrains TextMate-bundle installer (reuses the VS Code grammar)
├── web/                     # Single-file browser playground
│   ├── index.html           # Playground + landing page
│   └── bovnar_parser.js     # Dependency-free JavaScript parser
├── doc/
│   ├── 0_bovnar_tutorial.md
│   ├── 1_bovnar_spec.md            # Format specification (v1.1)
│   ├── 2_bovnar_unit_system.md
│   ├── 3_bovnar_readwrite_api.md
│   ├── 4_bovnar_python_bindings.md
│   ├── 5_bovnar.ebnf               # Formal EBNF grammar
│   ├── 6_bovnar_faq.md             # Frequently asked questions
│   ├── 7_bovnar_conformance.md     # Conformance test tool and IUT protocol
│   ├── 8_unit_cheatsheet.md        # Units & currencies quick reference
│   └── 9_bovnar_streaming.md       # Streaming, framing & multiplexing
├── CMakeLists.txt
└── CMakeLists_tests.txt
```

---

## Building

### Requirements

- CMake ≥ 3.21
- A C99-conforming compiler (GCC or Clang on Linux/macOS; **64-bit** MinGW64 or MSVC on Windows)
- No external library dependencies, at build time or runtime, beyond the C standard library (not even `libm`)

### Build the library and CLI tool

```bash
cmake -B build .
cmake --build build
```

This produces:

| Target | Path |
|---|---|
| Static library | `build/libbvnr.a` |
| Shared library | `build/libbvnr.so` |
| CLI binary | `build/bovnar` |

Build types: `Debug` (`-O0 -g3`), `Release` (`-O3 -flto`), `MinSizeRel` (`-Os`), `RelWithDebInfo` (`-O3 -g3 -flto`). `Release` and `RelWithDebInfo` enable link-time optimisation.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build
```

Every build also regenerates the single-file amalgamation into `build/amalgamate/`
(`bovnar.h` + `bovnar.c`) and packs release archives into the build directory
(`<count>` is the git commit count):

| Archive | Contents |
|---|---|
| `bovnar-<version>-<count>-amalgamate.tar.xz` | the amalgamation (`bovnar.h`, `bovnar.c`), plus `LICENSE`, `README.md`, `doc/` and `examples/` |
| `bovnar-linux-<version>-<count>.tar.xz` | Linux libraries, CLI and headers, plus `doc/`, `examples/` and the editor `highlighter/` grammars |
| `bovnar-windows-<version>-<count>.zip` | Windows libraries, CLI and headers (from the MSVC/MinGW build, or the `BVNR_CROSS_MINGW` cross-build on a Linux host), plus `doc/`, `examples/` and the editor `highlighter/` grammars |

Disable with `-DBVNR_PACKAGE=OFF`.

### Build options

| Option | Default | Effect |
|---|---|---|
| `BVNR_HARDEN` | `ON` | Stack protector (CLI), `_FORTIFY_SOURCE=2`, and `-Wformat` security checks. |
| `BVNR_WERROR` | `OFF` | Promote all warnings to errors (for CI). |
| `BVNR_FUZZ_TEST` | `ON` | Build the self-contained fuzz harnesses and register them as CTest tests. |
| `BVNR_FUZZ_EXTERNAL` | `OFF` | Build libFuzzer / AFL++ targets (requires clang or afl-clang-fast). |
| `BVNR_PACKAGE` | `ON` | On each build, regenerate the amalgamation into `build/amalgamate/` and pack release archives into the build dir. |
| `BVNR_SANITIZE` | `OFF` | ASan + UBSan (`-fno-sanitize-recover=all`) plus `-ftrivial-auto-var-init=pattern` where the compiler has it. GCC/Clang only; a diagnostic build, never a release one. |

```bash
cmake -B build -DBVNR_WERROR=ON .
```

**Running the sanitizer build.** The `bvnr_py_*` tests load `libbvnr.so` through
ctypes into an unsanitized interpreter, which ASan refuses outright, so either
skip them or preload the runtime:

```bash
cmake -S . -B build-asan -DBVNR_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
ctest --test-dir build-asan -E '^bvnr_py_'          # C suite

LD_PRELOAD=$(gcc -print-file-name=libasan.so) ASAN_OPTIONS=detect_leaks=0 \
    BVNR_LIB=$PWD/build-asan/libbvnr.so python3 -m pytest python
```

`-ftrivial-auto-var-init=pattern` is not a sanitizer and does not report
anything: it fills uninitialized automatic variables with a conspicuous pattern
so a read of one yields a distinctive value instead of whatever the stack held,
which makes a test that depends on such a read change its answer. GCC has no
MemorySanitizer; uninitialized *heap* reads are not covered by any of the above
and need a clang/MSan run.

### Link against the library

```bash
gcc my_app.c -I include -L build -lbvnr -o my_app
```

Both libraries share the base name `bvnr` (`libbvnr.a`, `libbvnr.so`), so `-lbvnr`
links the shared object when both are present. To link the static archive
instead, use `-l:libbvnr.a` (GNU ld) or static-link flags.

### Install (system / distro)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build
cmake --install build --prefix /usr/local
```

This installs the shared and static libraries (with the versioned `libbvnr.so.2`
soname chain), the `bovnar` CLI, all public headers, a **pkg-config** file, and a
**CMake package**. Downstream projects can then use either:

```bash
# pkg-config
gcc my_app.c $(pkg-config --cflags --libs bvnr) -o my_app
```

```cmake
# CMake
find_package(bovnar REQUIRED)
target_link_libraries(my_app PRIVATE bovnar::bvnr)        # or bovnar::bvnr_static
```

The freedesktop MIME entry is installed too (disable with `-DBVNR_INSTALL_MIME=OFF`);
after install, run `update-mime-database <datadir>/mime` to register the `.bvnr` type.

### Windows (64-bit)

Both 64-bit toolchains are supported with the same `cmake` invocation; each
builds into its own subdirectory so the two can coexist in one checkout:

```bash
# MSVC (x64 Developer environment, or the Visual Studio generator)
cmake -B build/msvc -A x64 .
cmake --build build/msvc --config Release   # -> bvnr.dll + bvnr.lib, bvnr_static.lib, bovnar.exe

# MinGW64
cmake -B build/mingw -G Ninja -DCMAKE_BUILD_TYPE=Release .
cmake --build build/mingw                   # -> libbvnr.dll + libbvnr.dll.a, libbvnr.a, bovnar.exe
```

On MSVC the static archive is `bvnr_static.lib` because the DLL's import library
already takes `bvnr.lib`. The C/Python test suite is POSIX-only and not built on
Windows; the `Build & Package` CI workflow builds both Windows toolchains and a
native Linux target, smoke-tests the CLI, and publishes the artifacts.
Consumers passing their own file descriptors to the fd source/sink on Windows
must open them in binary mode (`_O_BINARY`); the `bovnar` CLI does this itself.

---

## Command-Line Tool

The `bovnar` binary built above wraps the library for everyday use:

| Command | Description |
|---|---|
| `bovnar validate <file>` | Validate a `.bvnr` file; exit non-zero on the first error. |
| `bovnar query <path> <file>` | Print a single value by dotted path, e.g. `.sensor.temperature`. |
| `bovnar pretty-print <file>` | Re-serialise a document in canonical pretty form. |
| `bovnar convert <file>` | Convert between `json` and `bvnr`; direction is auto-detected from the `.json`/`.bvnr` extension. Add `--from <fmt> --to <fmt>` to override. |
| `bovnar events [-c] [-d] [-p] <file\|->` | Print the lexer (unverified) and validator (verified) event streams side by side. `-c` resync on error, `-d` debug re-serialisation, `-p` pretty debug output. Pass `-` to read stdin. (`-d` re-serialises the event stream and re-emits the source's `#!bovnar` version directive, so its debug output round-trips even for spec-1.1-gated values.) |
| `bovnar frames pack\|list <file…\|->` | Wrap each document in a length-prefixed frame, or list the documents in a frame stream. |
| `bovnar mux pack\|list <chan:file…\|->` | Multiplex files onto channels in one octet stream, or list the channel/message sizes in a multiplexed stream. |
| `bovnar version` | Print the library version and supported spec version. |
| `bovnar bench [options]` | Benchmark parsing throughput across profiles and payload sizes; `--json` for machine-readable output. |

```bash
bovnar validate examples/units.bvnr
bovnar query .system.host config.bvnr
bovnar convert data.json          # json -> bvnr (direction from extension)
bovnar convert data.bvnr          # bvnr -> json
cat data.bvnr | bovnar events -
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

| Binary / test | Coverage |
|---|---|
| `bvnr_reader_test` | Core reader, all token types |
| `bvnr_extended_reader_test` | Edge cases, resync, error recovery |
| `bvnr_writer_test` | Serialiser output |
| `bvnr_socketpair_roundtrip_test` | Full round-trip over a POSIX socketpair |
| `bvnr_stream_test` | Framing, multiplexing, and document-in-document streaming |
| `bvnr_dom_test` | DOM builder and traversal |
| `bvnr_si_test` | SI/IEC unit parsing and formatting |
| `bvnr_unit_ext_test` | Extended unit symbols, long-name aliases, prefix enforcement |
| `bvnr_currency_test` | Fiat and crypto currency lookup, minor units, prefix rules |
| `bvnr_utils_test` | Utility functions |
| `bvnr_int_test` | Arbitrary-precision integer arithmetic |
| `bvnr_float_test` | Floating-point representation |
| `bvnr_float_fix_dec_test` | Fixed and decimal float modes |
| `bvnr_datetime_test` | Datetime parsing, epochs, ISO-8601 literals, and Gregorian conversions |
| `bvnr_high_severity_test` | Robustness under malformed input |
| `bvnr_conformance` | 319-case conformance suite — self-test plus `--iut` adapter mode |
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

Pure-Python tests (`ctest -L python_pure`) run without the shared library; integration tests (`ctest -L python_integration`) need `libbvnr.so`, whose path CTest injects automatically. To run pytest directly instead:

```bash
export LIBBOVNAR_PATH=$(pwd)/build/libbvnr.so
cd python && pip install -e ".[dev]"
pytest tests -v
```

---

## C API — Quick Start

### Reading from memory

Callbacks are registered through `bvnr_read_flags_t`. Both `on_unverified` and `on_verified` receive `(void *userdata, bvnr_event_t ev, bvnr_data_t *d)` and must return `true` to continue parsing.

```c
#include <stdio.h>
#include <string.h>
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
#include <stdio.h>
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
    buf[bvnr_writer_bytes_written(w)] = '\0';   /* NUL-terminate for puts */
    bvnr_writer_destroy(w);
    puts(buf);
}
```

See [doc/3_bovnar_readwrite_api.md](doc/3_bovnar_readwrite_api.md) for the complete API reference, including the low-level `bvnr_write_event` interface and the full set of typed write helpers (`bvnr_write_uint`, `bvnr_write_sint`, `bvnr_write_float_unit`, etc.).

---

## Python Bindings — Quick Start

### Requirements

- **Python ≥ 3.10**
- The shared library `libbvnr.so` (built as shown above)

### Installation

```bash
cmake -B build . && cmake --build build

export LIBBOVNAR_PATH=$(pwd)/build/libbvnr.so

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

### CLion (JetBrains)

Quit CLion first — it rewrites its config on exit and would discard the change —
then run:

```bash
cd highlighter/clion && ./install.sh
```

This registers the shared VS Code grammar (`highlighter/vscode/bovnar-highlight/`)
as a TextMate bundle in every detected CLion config, giving full `source.bovnar`
highlighting. Start CLion and open a `.bvnr` file. The bundle is read live from
that path, so keep it in place. Works in other JetBrains IDEs (IntelliJ IDEA,
PyCharm, GoLand, …) too.

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
| [Specification (v1.1)](doc/1_bovnar_spec.md) | Full lexical and syntactic grammar, type system, arrays, structs, octet streams, validation rules, and formal EBNF. |
| [Tutorial](doc/0_bovnar_tutorial.md) | Practical, hands-on introduction to the format. |
| [Unit System Reference](doc/2_bovnar_unit_system.md) | SI and IEC prefixes, base units, compound units, exponents, C API, and validation rules. |
| [Read & Write API](doc/3_bovnar_readwrite_api.md) | Complete C API for streaming readers and writers with annotated examples. |
| [Python Bindings](doc/4_bovnar_python_bindings.md) | Pure-ctypes Python interface: high-level `loads`/`dumps`, streaming `Reader`/`Writer`, unit helpers. |
| [Formal EBNF](doc/5_bovnar.ebnf) | Machine-readable grammar. |
| [FAQ](doc/6_bovnar_faq.md) | Frequently asked questions covering the format, type system, units, C API, Python bindings, and limits. |
| [Conformance Test Tool](doc/7_bovnar_conformance.md) | Conformance suite (319 cases), IUT protocol for verifying third-party implementations, TAP output, and CTest integration. |
| [Units & Currencies Cheat Sheet](doc/8_unit_cheatsheet.md) | Quick reference for every physical unit, 166 fiat currencies, and 50 cryptocurrencies, with prefix tables and symbol-disambiguation rules. |
| [Streaming, Framing & Multiplexing](doc/9_bovnar_streaming.md) | Endless streams, multi-document framing, octet multiplexing, and document-in-document — applications layered on the event API. |

See [`CHANGELOG.md`](CHANGELOG.md) for what changed between versions (including the additive spec 1.1).

---

## License

| Component | License |
|---|---|
| Source code (C, Python, CMake) | [MIT](LICENSE) |
| Documentation (`doc/`) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Examples (`examples/`) | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |

Copyright © 2026 Janos Sonntag.
