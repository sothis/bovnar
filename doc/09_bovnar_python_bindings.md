# Bovnar — Python Bindings

> **Spec version:** 1.2
> **Status:** Reference — the Python package as implemented
> **Scope:** Installation, the high-level and streaming APIs, the DOM, `Quantity`, and the NumPy and pint bridges.

Pure-`ctypes` Python bindings for the **Bovnar (BVNR)** typed serialisation
library (library 2.0.2, format spec 1.2).

No compiled extension module is needed — the bindings load `libbvnr.so`
through the standard `ctypes.CDLL` machinery the first time something needs
the C library; `import bovnar` itself never loads it.

---

## Table of Contents

1. [Requirements](#1-requirements)
2. [Installation](#2-installation)
3. [Library discovery](#3-library-discovery)
4. [Quick-start](#4-quick-start)
    - 4.1 [High-level API (`loads` / `dumps`)](#41-high-level-api-loads--dumps)
    - 4.2 [SAX-style streaming reader](#42-sax-style-streaming-reader)
    - 4.3 [Generator / iterator interface](#43-generator--iterator-interface)
    - 4.4 [DOM (random-access) API](#44-dom-random-access-api)
    - 4.5 [Low-level writer](#45-low-level-writer)
    - 4.6 [Streaming / framing (`bovnar.stream`)](#46-streaming--framing-bovnarstream)
    - 4.7 [Live streams: feeding in pieces](#47-live-streams-feeding-in-pieces)
    - 4.8 [Live streams: writing a message at a time](#48-live-streams-writing-a-message-at-a-time)
5. [Unit helpers](#5-unit-helpers)
    - 5.1 [The unit-profile notations (spec 1.2)](#51-the-unit-profile-notations-spec-12)
    - 5.2 [Extended unit functions](#52-extended-unit-functions)
    - 5.3 [`UnitFlags`](#53-unitflags)
    - 5.4 [`ValueUnitPrefix`](#54-valueunitprefix)
    - 5.5 [Inline unit suffix](#55-inline-unit-suffix)
    - 5.6 [`UnitPolicy` — validation and conversion without a callback](#56-unitpolicy--validation-and-conversion-without-a-callback)
    - 5.7 [Building a `ValueUnit`](#57-building-a-valueunit)
    - 5.8 [Validators and literal helpers](#58-validators-and-literal-helpers)
6. [`Quantity`](#6-quantity)
    - 6.1 [Construction](#61-construction)
    - 6.2 [Properties and methods](#62-properties-and-methods)
    - 6.3 [Lossless numeric access (`float_dec`, `float_fix`, `float:128`/`256`)](#63-lossless-numeric-access-float_dec-float_fix-float128256)
    - 6.4 [`dumps()` integration](#64-dumps-integration)
7. [`write_array` helper](#7-write_array-helper)
8. [pint bridge](#8-pint-bridge)
    - 8.1 [Prefixes ride in the unit, never the magnitude](#81-prefixes-ride-in-the-unit-never-the-magnitude)
    - 8.2 [Affine temperature units](#82-affine-temperature-units)
    - 8.3 [Validation](#83-validation)
    - 8.4 [Registry control: `build_registry`](#84-registry-control-build_registry)
    - 8.5 [Quantity kinds: `isolate_kinds`](#85-quantity-kinds-isolate_kinds)
    - 8.6 [Semantic caveats](#86-semantic-caveats)
9. [NumPy bridge](#9-numpy-bridge)
    - 9.1 [Reading: `to_numpy`](#91-reading-to_numpy)
    - 9.2 [Writing: `from_numpy` / `array_to_bvnr`](#92-writing-from_numpy--array_to_bvnr)
    - 9.3 [pint arrays](#93-pint-arrays)
10. [Currency helpers](#10-currency-helpers)
11. [`Reader` reference](#11-reader-reference)
    - 11.1 [Construction](#111-construction)
    - 11.2 [`read_mem`](#112-read_mem)
    - 11.3 [`read_fd`](#113-read_fd)
    - 11.4 [`read_file`](#114-read_file)
    - 11.5 [`iter_mem(data, *, verified_only, **options)`](#115-iter_memdata--verified_only-options)
    - 11.6 [Error-state properties](#116-error-state-properties)
    - 11.7 [`MAX_FILESIZE_BYTES`](#117-max_filesize_bytes)
    - 11.8 [`read_stream`](#118-read_stream)
12. [`Writer` reference](#12-writer-reference)
    - 12.1 [Construction class methods](#121-construction-class-methods)
    - 12.2 [Output retrieval](#122-output-retrieval)
    - 12.3 [Scalar write methods](#123-scalar-write-methods)
    - 12.4 [Extended integer writers](#124-extended-integer-writers)
    - 12.5 [Struct helpers](#125-struct-helpers)
    - 12.6 [Version directive](#126-version-directive)
    - 12.7 [Array helpers](#127-array-helpers)
    - 12.8 [Low-level `emit`](#128-low-level-emit)
    - 12.9 [Octets, embedded documents and mux streams](#129-octets-embedded-documents-and-mux-streams)
13. [DOM API](#13-dom-api)
    - 13.1 [`dom_parse(data) -> DomDoc`](#131-dom_parsedata---domdoc)
    - 13.2 [`DomDoc`](#132-domdoc)
    - 13.3 [`DomNode`](#133-domnode)
    - 13.4 [`DomType`](#134-domtype)
    - 13.5 [Building a tree (`NewNode`)](#135-building-a-tree-newnode)
14. [Running the test suite](#14-running-the-test-suite)
15. [Package layout](#15-package-layout)
16. [Error handling](#16-error-handling)
17. [FFI details](#17-ffi-details)
    - 17.1 [`ON_ERROR_FUNC` signature](#171-on_error_func-signature)
    - 17.2 [`BvnrWriteFlags` layout](#172-bvnrwriteflags-layout)
    - 17.3 [`write_string` behaviour](#173-write_string-behaviour)
    - 17.4 [`_resolve_unit` default](#174-_resolve_unit-default)
    - 17.5 [Signals inside a callback](#175-signals-inside-a-callback)
18. [`BaseUnit` enum](#18-baseunit-enum)
19. [Spec 1.1 additions](#19-spec-11-additions)

- [See also](#see-also)

---

## 1. Requirements

| Requirement | Notes |
|---|---|
| Python ≥ 3.10 | `dataclasses`, `enum.IntEnum`, union-type annotations (`X \| Y`) |
| `libbvnr.so` | Runtime only, and bundled inside the wheel — see *Library discovery* below |
| `numpy` ≥ 1.24 | **Optional** — only for the NumPy bridge (`pip install bovnar[numpy]`) |
| `pint` ≥ 0.22 | **Optional** — only for the pint bridge (`pip install bovnar[pint]`) |
| pytest ≥ 7 | Test suite only (`pip install bovnar[dev]`) |

`numpy` and `pint` are imported **lazily, on first use** of their respective
bridge functions — importing `bovnar` never requires either to be installed.

---

## 2. Installation

```bash
# From PyPI. The wheels bundle libbvnr, so there is nothing to build and no
# system dependency to install.
pip install bovnar
```

Optional extras pull in the dependencies for the bridges:

```bash
pip install "bovnar[numpy]"   # NumPy bridge
pip install "bovnar[pint]"    # pint bridge
pip install "bovnar[all]"     # both numpy and pint
```

From a source checkout instead. `pyproject.toml` lives at the **repository
root**, not under `python/` — the pure-Python package there is mapped into the
wheel by `[tool.scikit-build] wheel.packages`, so the editable install runs from
the root:

```bash
cmake -B build . && cmake --build build      # produces build/libbvnr.so
export LIBBOVNAR_PATH="$PWD/build/libbvnr.so"

pip install -e ".[dev]"                      # from the repository root
```

---

## 3. Library discovery

The bindings search for `libbvnr.so` in this order:

1. **`LIBBOVNAR_PATH`** — absolute path to the `.so` file, e.g.
   ```bash
   export LIBBOVNAR_PATH=/opt/bovnar/lib/libbvnr.so
   ```
2. **`LIBBOVNAR_DIR`** — directory that *contains* the `.so`, e.g.
   ```bash
   export LIBBOVNAR_DIR=/opt/bovnar/lib
   ```
3. **Bundled in the package** — the library sitting next to `_ffi.py` inside the
   installed `bovnar` package. This is the normal path after `pip install
   bovnar`: CMake installs `libbvnr.*` into the package directory when the wheel
   is built, so an installed wheel resolves here and needs no environment
   variable at all.
4. **`ctypes.util.find_library('bvnr')`** — standard `ldconfig` / `LD_LIBRARY_PATH` search.
5. **In-tree build paths** — `../../build/`, `../../build/release/`, `../../`,
   `.` (resolved relative to the `_ffi.py` file; useful when building Bovnar
   alongside the bindings from a mono-repo).

The search runs on first use, not at import: `import bovnar` succeeds without
the library, and the first call that needs it (`loads`, `Reader()`,
`parse_unit`, …) raises `BovnarLibraryNotFound` if none of the candidates
loads. The message lists every candidate tried, in order, with why each one
failed (`not found`, or the loader's error).

---

## 4. Quick-start

### 4.1 High-level API (`loads` / `dumps`)

```python
import bovnar

# Serialise a Python dict to BVNR bytes
data = {
    "sensor_id": 42,
    "temperature": -3.7,
    "label": "outdoor",
    "active": True,
    "payload": None,
}
bvnr_bytes = bovnar.dumps(data)

# Deserialise back to a Python dict
recovered = bovnar.loads(bvnr_bytes)
assert recovered["sensor_id"] == 42
```

`nan`, `inf` and `ninf` come back as `float`, under an integer type as well
(`<uint:8> nan`, spec §6.4), as `dom_parse()` gives them.

`loads` and `load` take the read options of `Reader.read_mem` as keywords
(§11.2): `bovnar.loads(data, max_file_size=1 << 20, strict_version=True,
max_string_length=4096)` reads untrusted input under tighter limits than the
defaults.

`loads` accepts an optional `typed` flag that wraps typed values in `Quantity`
objects instead of decoding them to native Python scalars.  This preserves the
original text representation, exact type width, numeral base, and physical unit
for lossless round-trips:

```python
bvnr = b".pressure = <float:32,Pa> 101325.0;"
doc  = bovnar.loads(bvnr, typed=True)
q    = doc["pressure"]     # Quantity('101325.0', FLOAT [Pa])
print(q.raw)               # '101325.0'
print(q.unit_str)        # 'Pa'

# dumps() accepts Quantity values — annotation and raw text are re-emitted as-is
out = bovnar.dumps(doc)
assert bovnar.loads(out, typed=True) == doc
```

Two `Quantity` objects are equal (and hash alike) when their literal, unit and
*effective* type agree: a width of 0 is 64 and a radix of 0 is 10, as the C
library reads them, so `<float:64,_10> 5.7`, `<float:0> 5.7` and a bare `5.7`
are one value. (The base field of `float_fix` and `datetime` is Q and the epoch
index, and is compared as stored.) `dumps()` writes the canonical annotation,
which is why the assertion above holds for a document that spelled the
defaults out. It also holds for a quoted numeric literal (`<float:64> "5.7"`)
and for a typed null (`<float:64> ;`, `[<uint:32>, <sint:64>]`): `dumps()`
writes the annotation either one needs to read back as itself.

`dumps()` starts with a 4 MiB write buffer and doubles it automatically on
overflow, up to 256 MiB.  The `cap` keyword argument that existed in earlier
versions is no longer accepted.

`bovnar.load(f)` and `bovnar.dump(obj, f)` are the two for a socket or a binary
file object, with the same options and the same result: `load` pulls the bytes
as the parse needs them and `dump` pushes the output through the writer's
64 KiB buffer, so neither holds the whole document
([`Reader.read_stream`](#118-read_stream), `Writer.to_stream`). The one
difference is a failure on the way out: what `dump` pushed before a refused
value has already reached the stream.

```python
import io

buf = io.BytesIO()
bovnar.dump({"sensor_id": 42, "label": "outdoor"}, buf)
buf.getvalue() == bovnar.dumps({"sensor_id": 42, "label": "outdoor"})   # → True
bovnar.load(io.BytesIO(buf.getvalue()))["label"]                        # → 'outdoor'
```

`dumps()` takes `None`, `bool`, `int` of any size, `float`, `str`, `Decimal`,
a terminating `Fraction`, `bytes` / `bytearray` / `memoryview` (written as an
octet stream — which is what `loads()` returns as `bytes`), `Quantity`, a
`dict` with `str` keys, and a `list` / `tuple` of those. NumPy scalars are
taken as the Python numbers they are.

> **`dumps()` writes what `loads()` reads — the streaming tier, not the
> document tier.** `loads()` checks the grammar and each value's own type,
> unit and range and stops there; `dumps()` is its inverse and stops in the
> same place, so `dumps(loads(x))` works for every `x` that `loads()` accepts.
> Three shapes are therefore written without complaint and then **refused by
> the document tier** (`dom_parse`, `bovnar validate`), where an array is
> homogeneous and rectangular:
>
> | Python value | Written as | Document tier says |
> |---|---|---|
> | `[1, "a"]`, `[True, 1]` — a list mixing kinds | `[1, "a"]` | `array_element_type_mismatch` |
> | `[[1], [2, 3]]` — a ragged list of lists | `[[1], [2, 3]]` | `array_row_size_mismatch` |
> | `[{"a": 1}, {"b": 2}]` — dicts that do not share their keys | `[{.a = <uint:64> 1;}, {.b = <uint:64> 2;}]` | `struct_shape_mismatch` |
>
> A list mixing `int` and `float`, or holding `None` beside values, is fine at
> both tiers. If the output has to pass `bovnar validate`, run it through
> `dom_parse()` before shipping it.
>
> What `dumps()` does refuse, with `BovnarArgumentError`, is what *neither*
> tier reads: a list whose elements carry different units, an integer wider
> than 64 bits in a list that cannot take the width annotation it needs
> (`[2**70, 1.5]`), a `Decimal` past the range of `float_dec:64`, a key
> that is not a `str`, a key that is not a Bovnar identifier (spec §4.2:
> `"a b"`, `"1a"`, `"a.b"`, `""`), a key longer than 255 bytes in UTF-8 (the
> reader's default `max_identifier_length`, which `loads()`, `dom_parse()`
> and `bovnar validate` read with), and a string — or a key — holding a
> control character other than TAB, LF, VT, FF and CR (`"\x00"`, `"\x7f"`)
> or a lone surrogate. The message names the key.

### 4.2 SAX-style streaming reader

The verified callback receives exactly **two** positional arguments: the event
code and the data payload.  There is no userdata/context argument — capture
external state via closure instead.

```python
from bovnar.reader import Reader
from bovnar import Event

def on_event(ev, data):
    if ev == Event.ASSIGNMENT_START:
        print("key:", data.raw_str())
    elif ev == Event.DATA:
        print("value bytes:", data.raw_bytes())
    return True   # returning False stops parsing (raises BovnarParseError)

with Reader() as r:
    r.read_mem(bvnr_bytes, on_verified=on_event)
```

The `BvnrData` payload passed to a callback exposes:

| Method | Returns | Description |
|---|---|---|
| `data.raw_str(encoding='utf-8')` | `str` | The token bytes decoded as text |
| `data.raw_bytes()` | `bytes` | The raw token bytes |
| `data.converted_str()` | `str \| None` | Exact `want_unit` conversion result as a positional string, or `None` if no conversion / it does not terminate in its base |
| `data.converted_in_base(base)` | `str \| None` | The exact conversion re-rendered in `base` |
| `data.converted_rational()` | `tuple[int, int] \| None` | The exact conversion as `(numerator, denominator)` |
| `data.frac_str()` | `str \| None` | Verbatim ISO sub-second digits of a `datetime` literal (spec 1.1), or `None` |
| `data.unit_source_str()` | `str \| None` | The unit exactly as the document spelled it (library 2.0), or `None` when the value carries no unit |

It also carries the `converted` (bool) and `value_type` / `value_unit` fields directly.

**A payload may outlive its callback.** The `BvnrData` a callback (or a
`want_unit` hook) receives is a copy the binding owns, not a view of the
reader's memory: the token bytes, the sub-second digits, the unit spelling and
the exact conversion (`conv.num`/`conv.den`) are all copied out before the
callback runs. A callback can keep the object, and every method in the table
above returns the same answer after the callback has returned and after the
reader is closed.

### 4.3 Generator / iterator interface

```python
from bovnar.reader import Reader

with Reader() as r:
    for payload in r.iter_mem(bvnr_bytes):
        print(payload.event, payload.text)
```

### 4.4 DOM (random-access) API

```python
import bovnar

dom_bytes = b"""
.sensor_id = <uint:64> 42;
.config = {
    .host = "localhost";
    .port = <uint:16> 8080;
};
.values = [1.5, 2.5, 4.0];
"""
doc = bovnar.dom_parse(dom_bytes)

# Top-level key lookup
node = doc["sensor_id"]
print(node.as_i64())        # → 42
print(node.value_type)      # ValueTypeSpec(family=UINT, width=64, base=0)

# Struct traversal
cfg = doc["config"]
host = cfg["host"].as_str()
host                        # → "localhost"

# Array access
arr = doc["values"]
for i in range(len(arr)):
    print(arr[i].as_float())    # 1.5, 2.5, 4.0

# Convert entire document to a plain Python dict (drops type/unit info)
d = doc.to_dict()
d["config"]["port"]         # → 8080
```

### 4.5 Low-level writer

```python
from bovnar.writer import Writer
from bovnar.enums  import BaseUnit, SIPrefix

with Writer.to_mem() as w:
    w.write_float("velocity", 9.81, width=64,
                  unit_si_base=BaseUnit.METER,
                  unit_si_prefix=SIPrefix.NONE)
    w.write_uint("count", 1024, width=32)

output: bytes = w.get_output()
```

### 4.6 Streaming / framing (`bovnar.stream`)

Bindings for the streaming layer (see
[Streaming, Framing & Multiplexing](10_bovnar_streaming.md) for the full
treatment):

```python
from bovnar import stream

# Multi-document record framing
blob = stream.dump_documents([{"id": 1}, {"id": 2}])   # list[dict] -> bytes
docs = stream.load_documents(blob)                      # bytes -> list[dict]
docs = stream.load_documents(blob, continue_past_failed=True)  # bad docs -> None

# Octet multiplexing: (channel, payload) of any size, interleaved & reassembled
multiplexed = stream.mux_dump([(1, b"hello"), (42, b"world")])
messages    = stream.mux_load(multiplexed)             # [(1, b"hello"), (42, b"world")]

# Document-in-document
outer = stream.embed_document(bovnar.dumps({"v": 1}), key="payload")
inner = stream.parse_embedded(bovnar.loads(outer)["payload"])   # {"v": 1}
```

`parse_embedded` raises `BovnarParseError` carrying the inner document's own
error code and position. `mux_dump` takes channels in the unsigned 64-bit
range and raises `BovnarArgumentError` for anything outside it. The `key` of
`mux_dump`, `MuxWriter` and `embed_document` is held to the rule `dumps()`
holds a key to (§4.1), with the same `BovnarArgumentError`; the `key` filter
of `mux_load` and `DemuxFeed` may not hold a NUL.

`mux_dump(..., preamble={...})` writes a dict of ordinary values before the
stream opens — what the stream is, read by a consumer before its first message
(§4.7). `load_documents` and `mux_load` also take a socket or a binary file
object in place of bytes and read it to its end, parsing as they pull;
`frame_documents`, `dump_documents`, `mux_dump` and `embed_document` take a
`file=` to write to instead of returning bytes. The results are those of the
in-memory calls.

`load_documents`, `mux_load` and `parse_embedded` take the read options of
`Reader.read_mem` (`bovnar.READ_OPTIONS`, §11.2) as keywords, and read under
them: `load_documents` each document, with `max_file_size` capping one
document and `max_document_size` one frame; `mux_load` the document around the
stream. All three read with no total cap by default.

To write a mux stream, or an embedded document, inside a document of any
shape — values after it, more than one stream — use the `Writer` (§12.9):
`mux_begin(key)`, `mux_send(channel, payload)`, `mux_end()` and
`embed_document(key, doc)`. `mux_dump` and `MuxWriter` end the document with
their stream.

### 4.7 Live streams: feeding in pieces

The functions above take the whole input, or read a stream to its end. For
input that arrives when it arrives — a non-blocking socket, an event loop, a
queue — three objects in `bovnar.stream` each keep one C reader for the whole
stream and are handed bytes in pieces of any size, cut anywhere:

| Object | Fed | Hands out |
|---|---|---|
| `DocumentFeed` | one document | `DocumentEvent(event, data)` per verified event, as a `Reader` callback receives it; with `values=True`, `Value(key, value)` per top-level value once it is complete as well (`events=False` for the values alone) |
| `DemuxFeed` | a document carrying a mux stream | `Message(channel, payload)` per message; with `values=True` the values before and after the stream, in document order; with `events=True` the document's events |
| `FrameFeed` | a `BVF1` frame stream | each document's bytes; with `parse=True` each document parsed, as `load_documents` parses it |

All three take the read options of `Reader.read_mem` (`bovnar.READ_OPTIONS`,
§11.2): `max_file_size`, the text, array, length and nesting caps,
`continue_on_error`, `strict_version`, `text_only` and the rest. Besides those:
`DocumentFeed` takes `typed`, `want_unit` and `on_error`; `DemuxFeed(key=None,
max_message=0, typed=False, view=False)`, whose `recovery_count`,
`skipped_bytes` and `declared_version` are those of its reader, as on
`DocumentFeed`; `FrameFeed(parse=False, typed=False, continue_past_failed=False,
max_document_size=0, view=False)`, where `typed`, `continue_past_failed` and
the read options need `parse=True` (each parsed document is read under them,
`max_file_size` capping one document) and `view` needs `parse=False`
(otherwise `BovnarArgumentError`). `DemuxFeed`'s `key=None` demultiplexes every
octet stream of the document, as `mux_load` does, while `mux_dump` and
`MuxWriter` write under `key="mux"`: a document with any other octet stream —
a `bytes` value in the preamble, say — needs `key=` on the reader.

`feed(data)` returns the object itself, which iterates over what those bytes
completed, and `finish()` says the input is over. Running out of input is never
an error; `finish()` raises `BovnarParseError` for a document or frame cut off
part-way. Every other error carries the code the batch function gives —
`loads`, `mux_load`, `load_documents` (a failed document's own code, a bad
magic `UNEXPECTED_INPUT_BYTE`, a frame over `max_document_size`
`VALUE_OUT_OF_RANGE`, a stream cut inside a frame `GOT_INCOMPLETE_BVNR_STREAM`).
`DocumentFeed` raises from the `feed()` whose bytes hit the error, with the
reader's position, and drops what that feed had completed; `DemuxFeed` and
`FrameFeed` raise from the iteration that reaches it, after the items before
it (an exception from one of your own callbacks comes out of the call it was
raised in). `finish()` raises at once. Each is a context manager that frees
its C object.

A stream that starts with metadata hands it over before the first message:

```python
from bovnar import stream

doc = stream.mux_dump([(1, b"frame-1"), (2, b"audio"), (1, b"frame-2")],
                      key="media", preamble={"meta": {"codec": "av1"}})
doc += b".end = true;\n"

items = []
with stream.DemuxFeed(key="media", values=True) as dm:
    for at in range(0, len(doc), 16):          # as a socket might deliver it
        items.extend(dm.feed(doc[at:at + 16]))
    items.extend(dm.finish())

items[0]                             # → ('meta', {'codec': 'av1'})
items[1]                             # → (1, b'frame-1')
items[-1]                            # → ('end', True)
isinstance(items[1], stream.Message) # → True
```

`Message`, `Value` and `DocumentEvent` are named tuples, so
`channel, payload = item` works, and `isinstance` tells them apart. The key
scope, `max_message` (0 selects `BVNR_MUX_DEFAULT_MAX`), the 4096-channel limit
and the error codes are those of `mux_load`.

A document that never ends is read a value at a time; a value handed over is
not kept, so memory stays flat:

```python
values = stream.DocumentFeed(events=False, values=True)
first  = list(values.feed(b'.reading = 1; .read'))
second = list(values.feed(b'ing = 2; .x = [1, 2];'))
last   = list(values.finish())
first                                # → [('reading', 1)]
second                               # → [('reading', 2)]
last                                 # → [('x', [1, 2])]
```

A scalar, a struct or an octet stream is complete at its end; an array at the
next key or the end of the document, because a further `/[…]` row would extend
it. An octet stream in a `DocumentFeed` is gathered whole before it is handed
over, so an endless one belongs in a `DemuxFeed` or is read by its events. The
text of a fed document is capped as `Reader`'s is, at 2 147 483 647 bytes
(`TEXT_DATA_TOO_LONG`, doc/10 §1), and an array at as many items: pass
`max_text_bytes=stream.BVNR_NO_LIMIT` (and `max_array_items`) for a document
that does not end. `DemuxFeed` and a parsing `FrameFeed` take both as well.
`DocumentFeed` takes the read options of `Reader.read_mem` and its `want_unit`
and `on_error`, not its event callbacks, which the feed's items stand in for;
with `values=True`, `typed=True` gives `Quantity` values as
`loads(typed=True)` does (`typed` without `values`, or neither `events` nor
`values`, is refused). A `want_unit` or `on_error` callback runs inside the C
call, so it may not close the feed or feed it again (`BovnarArgumentError`). One feed reads one
document: make a new one for the next.

```python
frames = stream.FrameFeed(parse=True)
blob = stream.dump_documents([{"id": 1}, {"id": 2}])
docs = list(frames.feed(blob[:30])) + list(frames.feed(blob[30:]))
frames.finish()
docs                                 # → [{'id': 1}, {'id': 2}]
frames.count                         # → 2
```

**The fed bytes.** `DemuxFeed` and the unparsed `FrameFeed` are drained with
`bvnr_demux_next` / `bvnr_frame_reader_next`, which point into the fed bytes:
those are kept until the iteration has run to its end, and feeding again
before then raises `BovnarArgumentError`. A `bytearray` is copied when fed, so
the usual `recv_into` loop may refill it at once.

**Memoryview payloads.** By default a payload is `bytes`. With `view=True`
(`DemuxFeed`, unparsed `FrameFeed`) it is a read-only `memoryview` of the
reader's own memory instead — the fed bytes for a message that lay whole in one
chunk, a reassembly buffer otherwise — which saves a copy per message and is
valid **only until the next call on the feed**: the next item, `feed()`,
`finish()` or `close()`. The feed releases the view then, so using it later
raises `ValueError` rather than reading memory that has been reused. Copy what
must be kept with `bytes(view)`. A buffer made from the view —
`memoryview(view)`, a slice of it, `numpy.frombuffer(view)`, a held export — is
not released with it, so while one is alive the next call raises
`BovnarArgumentError` and leaves the stream as it was, as often as it is
repeated; drop the buffer and the call goes ahead. Such a buffer also keeps the
feed's C memory alive: `close()` then closes the feed at once (it hands out
nothing more) but frees that memory only when the buffer goes. A message that
lay whole in the fed bytes is a view of those `bytes`, which a buffer made from
it keeps alive by itself, so that one never makes a call refuse.

```python
dm = stream.DemuxFeed(view=True)
it = dm.feed(stream.mux_dump([(1, b"abc"), (2, b"def")]))
first = next(it)
kept = bytes(first.payload)
second = next(it)                    # first.payload is released here
kept                                 # → b'abc'
try:
    first.payload[0]
except ValueError:
    pass                             # a released view refuses every use
dm.close()
```

**What it costs.** Every item crosses the ctypes boundary, so a message costs
microseconds in Python, not the nanoseconds of the C path (doc/10 §8.4).
`DemuxFeed` and the unparsed `FrameFeed` make one plain C call per item and no
callback; `DocumentFeed`, the parsing `FrameFeed` and the batch `mux_load` call
back into Python once per event or message. Measured with
`python/bench/bench_stream.py` (CPython 3.14, one core of an Intel i7-2700K
pinned with `taskset`, on a machine shared with other work: load average 3–4,
so take the ratios rather than the digits), per message, median of seven runs:

| Path | 150 B | 30 KB | 400 KB |
|------|------:|------:|-------:|
| `mux_load` (batch, one callback per message) | 4.8 µs | 36 µs | 383 µs |
| `DemuxFeed`, whole buffer, `bytes` | 3.3 µs | 7.1 µs | 81 µs |
| `DemuxFeed`, whole buffer, `memoryview` | 4.6 µs | 4.9 µs | 57 µs |
| `DemuxFeed`, 1460 B pieces, `bytes` | 4.2 µs | 130 µs | 1.6 ms |
| `DemuxFeed`, 1460 B pieces, `memoryview` | 5.7 µs | 131 µs | 1.6 ms |

A small message costs a few microseconds whichever way it is read. A large one
fed whole is sliced out of the fed bytes (or viewed in place) instead of
copied through a callback, which is the factor of about five. Fed in
1460-byte pieces — one TCP segment's payload — the cost is the feeds, a few
microseconds each, twenty of them for a 30 KB message; read from a socket, take
the largest `recv` the transport offers. A `memoryview` saves the copy only
where there is one to save, from a few kilobytes up.

### 4.8 Live streams: writing a message at a time

`mux_dump(..., file=)` and `dump_documents(..., file=)` take every message or
document at once. The writer behind `mux_dump` (and `embed_document`) gathers
its output in a 64 KiB buffer, so a reader at the other end of a socket sees
nothing until it fills; `dump_documents` and `frame_documents` write each frame
as it is made and call the object's `flush()` once at the end.
Two objects in `bovnar.stream` write as the messages come instead, each handing
what it writes to the socket or file before it returns:

| Object | Writes | Per call |
|---|---|---|
| `MuxWriter(file, *, key="mux", preamble=None, flush_each=True)` | a document carrying a mux stream, as `mux_dump` writes it | `send(channel, payload)`: one message |
| `FrameWriter(file, *, flush_each=True)` | a `BVF1` frame stream, as `frame_documents` writes it | `write(document_bytes)` or `dump(obj)`: one framed document |

`file` is a socket (`sendall`) or a binary file object (`write`). `MuxWriter`
writes the preamble and opens the stream when it is made (handing them to the
object at once with `flush_each=True`), and with `flush_each=True` it calls
`bvnr_write_flush` after every send: the message goes
to the object, then the object's `flush()` runs when it has one, so a
`DemuxFeed` at the other end has message N before message N+1 is sent. With
`flush_each=False` it batches into the 64 KiB buffer, and `flush()` hands over
what has gathered. `close()` — or leaving the `with` block normally — ends the
stream and the document; the object stays open. The bytes are `mux_dump`'s for
the same messages and preamble, however often they were flushed:

```python
import io
from bovnar import stream

out = io.BytesIO()
with stream.MuxWriter(out, key="media",
                      preamble={"meta": {"codec": "av1"}}) as mux:
    first_len = len(out.getvalue())     # the preamble is out already
    mux.send(1, b"frame-1")
    sent_len = len(out.getvalue())      # and so is the first message
    mux.send(2, b"audio")

first_len > 0                           # → True
sent_len > first_len                    # → True
same = out.getvalue() == stream.mux_dump(
    [(1, b"frame-1"), (2, b"audio")], key="media",
    preamble={"meta": {"codec": "av1"}})
same                                    # → True
```

Over a socket, the reader is a `DemuxFeed` fed whatever `recv` returns
(§4.7):

```python
# check-doc-python: skip
with stream.MuxWriter(sock, key="media", preamble={"meta": meta}) as mux:
    for channel, frame in frames():
        mux.send(channel, frame)        # on the wire when send returns
```

`FrameWriter` frames each document as it is written and hands it to the object
at once, as frames are never batched; its `flush_each` only says whether the
object's `flush()` follows each document. A frame stream has no terminator, so
`close()` only flushes the object. `MuxWriter.bytes_written` and
`FrameWriter.count` say how much has been written:

```python
out = io.BytesIO()
with stream.FrameWriter(out) as frames:
    frames.dump({"id": 1})
    frames.write(b'.id = 2;')
stream.load_documents(out.getvalue())   # → [{'id': 1}, {'id': 2}]
frames.count                            # → 2
```

The object has to be blocking: a non-blocking socket is refused when the
writer is made, since one that took part of a buffer and then raised
`BlockingIOError` would leave the stream cut mid-chunk. (A socket with a
timeout is accepted; a timeout fails the writer as any other exception does.)
An exception the object raises (a closed socket, a full disk) comes out of the
call that pushed — the constructor, `send`, `write`, `flush` or `close` —
unchanged. The writer has failed then: `MuxWriter.close()` only releases it,
and a `FrameWriter`, which may have written half a frame, refuses further
documents. Leaving a `MuxWriter`'s `with` block by an exception does not end the
stream either, so a reader sees it cut off rather than complete.
`Writer.to_stream(...).flush()` (§12.2) does the same for a document written
value by value.

**What it costs.** Measured with `python/bench/bench_stream.py` on the machine
of §4.7, into a socket-like object whose `sendall` takes the bytes, per
message, the median over its runs:

| Path | 150 B | 30 KB | 400 KB |
|------|------:|------:|-------:|
| `mux_dump` (batch, to `bytes`) | 7.6 µs | 29 µs | 287 µs |
| `MuxWriter`, `flush_each=False` | 2.2 µs | 10 µs | 114 µs |
| `MuxWriter`, flushed per message | 5.5 µs | 12 µs | 126 µs |

A flush per message costs a ctypes call and a Python call of the object's
`sendall` (with the copy of the bytes it is handed) for every message, against
one per 64 KiB batched: about 3 µs on a small message, which is the price of
the reader having it at once. Large messages are dominated by the copy either
way.

---

## 5. Unit helpers

```python
import bovnar

# Parse a unit string into a ValueUnit struct
vu = bovnar.parse_unit("k~g·m/s²")

# Convert a ValueUnit back to its canonical string
s = bovnar.unit_to_str(vu)     # → "k~g·m/s²"

# Scalar SI/IEC PREFIX factor for a unit string — the prefix only, never the
# base unit's own factor. Use units.unit_to_si_factor() for the full SI scale.
f = bovnar.unit_factor("M~Hz") # → 1_000_000.0
f = bovnar.unit_factor("in")   # → 1.0  (NOT 0.0254 — the inch has no prefix)
f = bovnar.unit_factor("h")    # → 1.0  (NOT 3600.0)
```

### 5.1 The unit-profile notations (spec 1.2)

**These notations are spec 1.2**: in a document they need a `#!bovnar 1.2` directive, while the API entry points below have no document and take them unconditionally. `parse_unit` takes a profile notation as readily as the native one, and returns
the same `ValueUnit` either way — so everything else in this chapter works on the
result unchanged. **Seven** namespaces are accepted — `ucum:`, `unece:`, `qudt:`,
`qudt-qk:`, `udunits:`, `om:` and `cf:` — but each is a *compile-time* switch, so
which of them a given build carries is a question to ask rather than assume; see
`available_profiles()` below. Helpers cover what a caller needs around them:

```python
import bovnar

vu = bovnar.parse_unit("ucum:mm[Hg]")
bovnar.unit_to_str(vu)              # → "mmHg"   — the native canonical form
bovnar.unit_to_ucum(vu)             # → "mm[Hg]" — back to a UCUM code
bovnar.units_compatible(vu, bovnar.parse_unit("k~Pa"))   # → True

# Which profiles THIS build carries. Each namespace is a compile-time switch,
# so a wheel may have been built without some of them. parse_unit raises the
# same BovnarArgumentError("Invalid unit string: ...") for an absent namespace
# as for a bad unit; unit_error_code() below is what tells the two apart (49
# against 32).
bovnar.available_profiles()
# → ('ucum', 'unece', 'qudt', 'qudt-qk', 'udunits', 'om', 'cf')

# The same unit in other vocabularies — all one ValueUnit
kg = bovnar.parse_unit("k~g")
bovnar.unit_to_profile("ucum",    kg)   # → "kg"
bovnar.unit_to_profile("unece",   kg)   # → "KGM"
bovnar.unit_to_profile("qudt",    kg)   # → "KiloGM"
bovnar.unit_to_profile("udunits", kg)   # → "kg"

iu = bovnar.parse_unit("ucum:[IU]/mL")
bovnar.unit_is_profile_only(iu)     # → True  — no native spelling exists
bovnar.unit_to_str(iu)              # → "ucum:[IU].mL-1"

box = bovnar.parse_unit("unece:XBX")
bovnar.unit_is_profile_only(box)    # → True  — a countable package
bovnar.unit_to_str(box)             # → "unece:XBX" — in its OWN namespace

bovnar.unit_error_code("ucum:B[SPL]")   # → 50 (ErrorCode.UNIT_PROFILE_UNSUPPORTED)
bovnar.unit_error_code("udunits:days since 1970-01-01")
                                        # → 50 — valid UDUNITS, not carryable
bovnar.unit_error_code("nosuch:m")      # → 49 (ErrorCode.UNIT_PROFILE_UNKNOWN)
bovnar.unit_error_code("m/s")           # → 0  (ErrorCode.NONE — it parses)
```

`unit_to_profile` and `unit_to_ucum` raise rather than inventing a code, in three
situations. A native unit outside that vocabulary's transliteration table has no
form in it — the Old German units, the water-hardness degrees, the turbidity
kinds and every currency. An opaque unit belonging to a *different* profile has
none either: `[IU]` is UCUM's, so `unit_to_profile("unece", …)` refuses it. And a
**flat** vocabulary (`unece`, `qudt`, `qudt-qk`, `om`) can spell only a single
component — no product, quotient or exponent, though a prefixed one is fine
(`k~g` is `unece:KGM`) — because a flat code is one whole token with no operator to
build it out of — so `k~m/h` has no UNECE form even though `unece:KMH` parses to
exactly it.

In a **document** the notation additionally requires a `#!bovnar 1.2` directive;
without it `loads` raises with `unit_illegal`. `parse_unit` has no document and
therefore no declared version, so it accepts the notation unconditionally.

See [Unit Profiles](11_bovnar_unit_profiles.md) for the transliteration tables
and the codes that have no representation.

### 5.2 Extended unit functions

The following functions operate on `ValueUnit` objects and are available both
from the top-level `bovnar` namespace and from `bovnar.units`. Each of them —
and `unit_to_str`, `unit_to_profile`, `unit_to_ucum`, `unit_is_profile_only`,
the pint and NumPy bridges, `write_array(vu=)` and a `want_unit` callback's
return value — takes a `Unit` (what `Quantity.unit` and `Unit.parse()` hand
out) wherever it takes a `ValueUnit`, so `unit_to_str(q.unit)` needs no
`.raw`.

```python
from bovnar import (
    unit_valid, unit_prefix_factor, unit_prefix_exponent,
    prefix_unit_valid,
    unit_to_si_factor, units_compatible, units_equal,
    unit_convert_factor, unit_dimension_vector,
    unit_reduce, unit_to_str_ex,
    exponent_to_int, int_to_exponent,
    convert_value,
    UnitFlags,
)

# Validate a ValueUnit struct
ok = unit_valid(vu)                   # True when vu is structurally valid

# Prefix scale factor (SI or IEC) for a ValueUnit
f = unit_prefix_factor(vu)            # e.g. 1000.0 for k~m, 2**30 for Gi~B

# Prefix exponent (base-10 for SI, base-2 for IEC)
e = unit_prefix_exponent(vu)          # e.g. 3 for kilo, -3 for milli, 30 for gibi

# Validate a prefix for a base unit (IEC prefixes are only valid on bit/byte)
from bovnar import ValueUnitPrefix, IECPrefix, BaseUnit
p = ValueUnitPrefix.make_iec(IECPrefix.GIBI)
ok = prefix_unit_valid(p, BaseUnit.BYTE)   # True
ok = prefix_unit_valid(p, BaseUnit.METER)  # False

# Full SI conversion including affine terms (e.g. Celsius → Kelvin)
conv = unit_to_si_factor(vu)
# conv.factor, conv.is_affine, conv.affine_offset

# Check convertibility: same SI dimension vector AND same quantity kinds
ok = units_compatible(vu_a, vu_b)     # b vs B is False — the dimensions agree,
                                      # the kinds do not

# Conversion factor between two compatible units
c = unit_convert_factor(vu_from, vu_to)
# c.factor, c.requires_affine, c.ok — ok is False (and factor 0.0) for an
# affine conversion whose offsets do not cancel, °C → K: use convert_value

# 7-element SI dimension exponent vector [m, kg, s, A, K, mol, cd]
dims = unit_dimension_vector(vu)       # e.g. [1, 0, -1, 0, 0, 0, 0] for m/s

# Fold every prefix out and combine repeated bases. NOT the named-SI collapse:
# unit_reduce(parse_unit("k~g·m/s²")).unit is "m·g/s²", and it is
# unit_to_str_ex(..., UnitFlags.REDUCE) that gives "N". See the warning in §5.3
# before applying r.scale to a value you are also going to serialise.
r = unit_reduce(vu)                   # r.unit, r.scale

# Convert a scalar value between units (handles affine conversions)
kelvin = convert_value(25.0, vu_celsius, vu_kelvin)

# ... or EXACTLY, as the reader's want_unit does: (Fraction, exact). exact is
# False for an irrational factor (° to rad); a float is refused, pass Fraction(x)
from fractions import Fraction
from bovnar import convert_rational
v, exact = convert_rational(42, parse_unit("k~m/h"), parse_unit("m/s"))
# v == Fraction(35, 3), exact is True

# Serialise with formatting options (see UnitFlags below)
s = unit_to_str_ex(vu, UnitFlags.ASCII_EXP)   # use ^N instead of Unicode superscripts
s = unit_to_str_ex(vu, UnitFlags.REDUCE)      # reduce first — but see the warning below
s = unit_to_str_ex(vu, UnitFlags.REDUCE | UnitFlags.ASCII_EXP)

# Exponent enum ↔ integer conversions
n   = exponent_to_int(Exponent.NEG_SQUARE)  # → -2
exp = int_to_exponent(-2)                    # → Exponent.NEG_SQUARE
```

`SI_DIM_NAMES` is the ordered tuple `('m', 'kg', 's', 'A', 'K', 'mol', 'cd')`
— the index positions used by `unit_dimension_vector`.

### 5.3 `UnitFlags`

```python
from bovnar import UnitFlags   # also from bovnar.units

UnitFlags.NONE      # 0 — no special formatting
UnitFlags.REDUCE    # reduce to a canonical named SI unit before serialising
UnitFlags.ASCII_EXP # use ^N exponent notation instead of Unicode superscripts
UnitFlags.NUM_CANONICAL  # writer only: a float REDUCE rescaled keeps float shape
```

The same flags configure a **writer**: `Writer.to_mem(unit_flags=UnitFlags.REDUCE)`
(and `to_fd` / `to_file`) rewrites each unit as it is written *and* rescales the
value with it — `2.5 k~g` goes out as `<float:64,g> 2500`. `NUM_CANONICAL` is
meaningful only there: it appends `.0` to a rescaled float that came out as an
integer string, and writes each annotation in one form (the width written out,
a default `_10` base and `no_unit` left out), so the canonical document mode
lands on one spelling.

> **`UnitFlags.REDUCE` returns the reduced unit, not a rescaled value.** Reduction
> folds every prefix out, so `unit_to_str_ex(parse_unit("k~g"), UnitFlags.REDUCE)`
> is `"g"` — a string denoting a quantity 1000× smaller than the unit you passed.
> Moving the value with it is your job. (The writer does it for you; nothing else
> does.)
>
> **Do not use `unit_reduce(vu).scale` for that.** It is the scale to the *fully
> reduced* unit, and this function does not always emit the fully reduced unit:
> where the reduction lands on a named SI unit the formatter re-attaches the
> prefix. `k~N` comes back `"k~N"` with nothing to rescale while
> `unit_reduce` still reports `1000.0`, and `k~g` comes back `"g"` where the
> `1000.0` must be applied. The two are indistinguishable from outside — both a
> lone unit carrying a kilo prefix — so take the scale from the unit that is
> actually emitted:
>
> ```python
> emitted_str = unit_to_str_ex(vu, UnitFlags.REDUCE)
> emitted     = parse_unit(emitted_str)
> value      *= convert_value(1.0, vu, emitted)   # 1.0 for k~N, 1000.0 for k~g
> ```
>
> The collapse never substitutes one named unit for another, so a `Sv` stays a
> `Sv` rather than becoming a `Gy`. A lone base at an exponent other than 1 does
> still collapse (`s⁻¹` → `"Hz"`, `m~s⁻¹` → `"k~Hz"`), and a reduction whose summed
> exponent leaves the `±100` range (`BVN_EXPONENT_MIN`/`MAX`) raises
> `BovnarArgumentError` instead of returning a unit that lost a dimension —
> `m⁵⁰·m⁵⁰` reduces to `m¹⁰⁰`, `m⁵⁰·m⁵¹` raises.

`UnitFlags` is an `IntFlag` and its values may be OR-combined:

```python
s = unit_to_str_ex(vu, UnitFlags.REDUCE | UnitFlags.ASCII_EXP)
```

### 5.4 `ValueUnitPrefix`

`ValueUnitPrefix` is the public mirror of the C `value_unit_prefix_t` struct.
It can be constructed with class methods or extracted from a `ValueUnitComponent`:

```python
from bovnar import ValueUnitPrefix, SIPrefix, IECPrefix

p_si  = ValueUnitPrefix.make_si(SIPrefix.KILO)
p_iec = ValueUnitPrefix.make_iec(IECPrefix.GIBI)

vu   = bovnar.parse_unit("Gi~B")
comp = vu.components[0]
p    = comp.prefix   # ValueUnitPrefix extracted from a component
```

### 5.5 Inline unit suffix

In addition to the unit embedded in a type annotation (`<float:64,m/s>`), the
Bovnar format supports an **inline unit suffix** placed directly after a scalar
value, before the terminating `;`:

```bovnar
.speed = 9.81 m/s;           # inline suffix, no annotation
.mass  = <float:64> 70.5 k~g;# annotation without unit, inline adopted
.dist  = <float:64,m> 1.5 m; # annotation and inline both say 'm' — valid
```

From the Python layer, inline and annotation units are **identical**: both
reach the application as `data.value_unit` inside the `Event.DATA` payload.
No extra code is required to consume inline units.

The validator raises `ErrorCode.UNIT_MISMATCH` (38 / `BovnarParseError`) when
an inline suffix is present and a type-annotation unit is also present but the
two do not resolve to the same `value_unit_t`. Inline unit suffixes inside
array elements always raise `ErrorCode.UNEXPECTED_INPUT_BYTE`.

### 5.6 `UnitPolicy` — validation and conversion without a callback

`Reader.set_unit_policy` states what the document must contain and what unit
values should arrive in. Everything is unit **text**, so unlike `want_unit` it
costs no per-value trampoline across the FFI boundary — the C side parses the
strings once, when the policy is set.

```python
from bovnar import Reader, UnitPolicy

with Reader() as r:
    r.set_unit_policy(UnitPolicy(
        targets=["m/s", "°C"],      # convert to the first one that fits
        normalise_si=True,          # ...and everything else to SI base units
        leave_inexact=True,         # step over what cannot be exact
        require_unit=True,          # reject any bare numeric value
    ))
    r.read_file("sensors.bvnr", on_verified=handler)
```

A policy survives re-reading the same reader on another document — it describes
the consumer, not the document — and `set_unit_policy(None)` clears it. Every
unit string is parsed by the call, so a typo raises `BovnarArgumentError`
before a byte is read, and a rejected policy leaves the previous one in force.

**Conversion.** Each numeric value takes the first target it can validly convert
to, so order is significant: `["m", "k~m"]` never selects `k~m`. A value that
matches nothing, or that is already in the unit a target names, arrives
untouched — `data.converted` is what tells the two apart. Each target may carry
its own output base as `("m", 16)`.

`normalise_si` catches whatever the targets did not, delivering it in coherent
SI base units with prefixes folded out. Currencies and every **dimensionless**
unit (`%`, `ppm`, `dB`, `pH`, `rad`, `°`) are left as written rather than
reinterpreted.

**A value with no unit only ever matches a target that is itself `no_unit`.** A
bare number is dimensionally compatible with `%` and `ppm`, so without that
fence a policy naming `"%"` would deliver `0.25` as `25`.

**Exactness.** Nothing approximate is ever delivered. `leave_inexact=True` hands
over a value the conversion cannot deliver exactly — an irrational factor, a
non-terminating expansion (`42 km/h` is `35/3 m/s`), an exponent past the work
limit — in its native unit instead of raising. It applies only to policy-chosen
targets; a `want_unit` hook keeps its strict all-or-nothing contract.

**Validation.** `require_unit` rejects a document containing any bare numeric
value; `require_dimension_of=["m"]` requires every numeric value to be validly
convertible to at least one listed unit. Both are evaluated on the unit the
**document** wrote, before any conversion, and both raise
`ErrorCode.UNIT_MISMATCH`.

**Per-field rules** name one field by its key path, and are consulted before
everything else:

```python
from bovnar import Reader, UnitPolicy, UnitRule, dom_parse

policy = UnitPolicy(rules=[
    UnitRule(".inlet.temperature", "°C"),                  # convert this field
    UnitRule(".inlet.*", "m", convert=False),              # assert the subtree
])
```

A path ending in ``".*"`` names a subtree at any depth, and a prefix only
matches at a component boundary (``".in.*"`` does not claim ``".inlet.a"``).
Unlike a whole-document target, a rule is an **assertion**: a value it cannot be
applied to raises ``UNIT_MISMATCH``, because silence would defeat the point of
naming the field. ``convert=False`` asserts without converting.

**The DOM takes the same policy**, so a random-access consumer gets both halves:

```python
doc = dom_parse(text, UnitPolicy(rules=[UnitRule(".inlet.temperature", "°C")]))
doc.lookup(".inlet.temperature").as_float()    # 100.0, from a document in °F
```

A value the policy converted is STORED converted — digits, unit and base — and
an integer that converts to a fraction (5 g in kilograms is 0.005) is stored as
a float. A policy the library refuses raises `BovnarArgumentError` — the same
error, naming the same entry, as `Reader.set_unit_policy` — before anything is
parsed, rather than a parse error, so a mistake in the policy is never mistaken
for a fault in the document.

**The writer takes the same policy, validation half only.** A reader can only
reject a document somebody already wrote; `Writer.set_unit_policy` is what stops
a bare number reaching the file:

```python
from bovnar import Writer, UnitPolicy

w = Writer.to_mem()
w.set_unit_policy(UnitPolicy(require_unit=True, require_dimension_of=["m"]))
w.write_float("tank_level", 12.0, unit_str="in")   # a length — written
w.write_float("spare", 0.25)                        # BovnarWriteError,
                                                    # code UNIT_MISMATCH
```

A policy carrying `targets`, `normalise_si`, `base` or `leave_inexact` raises
`BovnarArgumentError` there rather than being half-honoured: write-side unit
rewriting is `UnitFlags.REDUCE`'s job — open the writer with
`Writer.to_mem(unit_flags=UnitFlags.REDUCE)` — and two rewriting modes with
different rules about exactness is how a document ends up meaning two things.

Module-level helpers back the same decisions outside the reader:

```python
from bovnar import (units_equal, units_compatible, units_convertible,
                    unit_si_normal_form, parse_unit)

# units_compatible is the wrong screen for a conversion target: a currency
# carries no dimension, so it is reported incompatible with itself.
units_compatible (parse_unit("k~$USD"), parse_unit("$USD"))   # → False
units_convertible(parse_unit("k~$USD"), parse_unit("$USD"))   # → True

unit_si_normal_form(parse_unit("in"))   # → m
unit_si_normal_form(parse_unit("g"))    # → k~g  (the SI base unit for mass)
unit_si_normal_form(parse_unit("%"))    # → None (dimensionless)
unit_si_normal_form(parse_unit("lm"))   # → None (carries the steradian's kind)
```

**Three questions, in decreasing strictness.** `units_equal` asks whether the two
are the *same* unit, `units_compatible` whether their dimensions and quantity
kinds line up, `units_convertible` whether a conversion can actually be
performed. Only the last is the right screen for a conversion target.

```python
units_equal(parse_unit("m"), parse_unit("m"))       # → True
units_equal(parse_unit("m"), parse_unit("k~m"))     # → False — convertible, not equal
units_equal(parse_unit("N"), parse_unit("k~g·m/s²"))  # → False — same SI form
units_equal(parse_unit("mmHg"), parse_unit("ucum:mm[Hg]"))   # → True
```

`units_equal` is also what `==` means between two units, whether they are
`ValueUnit`s or `Unit`s, and the two hash alike so they share a dict:

```python
parse_unit("m") == parse_unit("m")                  # → True
```

The hash follows the equality in the one place the two could part: a
component with **no prefix** is the same component whether its prefix system
says SI or IEC, so `parse_unit("B")` and `make_unit_iec(BaseUnit.BYTE)` are
equal *and* one dict key.

A `Unit` also compares equal to a **string** that parses to it
(`q.unit == "k~m"`). That is a convenience for comparison only and is **not
hash-compatible** — `"km"`, `"k~m"` and `"ucum:km"` are three strings with
three hashes and one unit — so `Unit.parse("m") in {"m"}` is `False`. Key a
dict or set on `Unit` objects (or on `str(unit)`), never on a mix of the two.

`Unit.convert_factor(other)` and `Unit.factor` return a bare number, so they
**raise** `BovnarArgumentError` where there is no such number: an affine pair
(`°C` → `K`) has no multiplicative factor and an affine unit no SI factor.
Use `convert_value()` there, or `unit_to_si_factor()` for the scale and the
offset.

That comparison used to be **False**. `ValueUnit` is a `ctypes.Structure`, which
defines no `__eq__`, so `==` fell back to identity — see the note in
[§5.7](#57-building-a-valueunit) on why the raw struct is worth knowing about at
all. `ValueTypeSpec` carried the same gap and compares by value now too, so
`node.value_type == other.value_type` answers the question it looks like it is
answering.

### 5.7 Building a `ValueUnit`

`parse_unit` builds a unit from text. To build one programmatically — without going
through the notation — use the `make_unit_*` constructors, exported from the top-level
`bovnar` namespace and from `bovnar.structs`.

```python
from bovnar import (make_unit_si, make_unit_iec, make_unit_compound,
                    make_unit_none, make_unit_dimensionless,
                    BaseUnit, SIPrefix, IECPrefix, Exponent, unit_to_str)

make_unit_si(BaseUnit.GRAM, SIPrefix.KILO)              # k~g
make_unit_si(BaseUnit.METER, exp=Exponent.SQUARE)       # m²
make_unit_iec(BaseUnit.BYTE, IECPrefix.GIBI)            # Gi~B

make_unit_compound([                                    # k~g·m/s²
    {"base": BaseUnit.GRAM,   "si_prefix": SIPrefix.KILO, "exp": Exponent.LINEAR},
    {"base": BaseUnit.METER,                             "exp": Exponent.LINEAR},
    {"base": BaseUnit.SECOND,                            "exp": Exponent.NEG_SQUARE},
])
```

`prefix` and `exp` default to none and linear, so a plain base unit is
`make_unit_si(BaseUnit.METER)`. A `make_unit_compound` component accepts the keys
`base`, `exp`, `si_prefix` and `iec_prefix` — note that the compound form spells the
prefix key out, where the scalar constructors take it positionally.

**`make_unit_none()` and `make_unit_dimensionless()` are not the same thing**, even
though both render as `no_unit`. The first is a unit struct with no components at all —
a value carrying no unit. The second is one component whose base is `NONE` — a value
carrying `no_unit` explicitly. That is exactly the pair `UnitPolicy(require_unit=True)`
refuses together (§5.6), and the distinction survives a round trip:

```python
unit_to_str(make_unit_none())            # 'no_unit'
unit_to_str(make_unit_dimensionless())   # 'no_unit'
bytes(make_unit_none()) != bytes(make_unit_dimensionless())   # True
```

### 5.8 Validators and literal helpers

`bovnar.literals` binds the C library's checks and literal helpers
(`bovnar.h`): the tests the writer applies to keys, symbols, references and
numbers, the integer and float parsers and formatters behind it, and the parser
of a type annotation. Each function is the C function of the same name without
its `bvn_` prefix, and answers as it does. Text holding NUL is none of the
things they check: a validator answers `False` for it, a parser raises.

**Type annotations.** `parse_type_annotation(text)` parses the text between the
angle brackets, as the reader does, into a frozen `TypeAnnotation` of
`value_type` (a `ValueTypeSpec`), `unit` (a `ValueUnit`, with no components when
there is no unit or it is spelled `no_unit`) and `unit_source` (the unit
parameter as written, `''` when there is none):

```python
from bovnar import literals, BovnarParseError

a = literals.parse_type_annotation("uint:32,_16,k~g")
a.value_type.width, a.value_type.base     # → (32, 16)
a.unit                                    # → "k~g"
a.unit_source                             # → "k~g"
literals.parse_type_annotation("float:64,ucum:mL{total}").unit_source   # → "ucum:mL{total}"

try:
    literals.parse_type_annotation("uint:8,_16,_16")    # a base given twice
except BovnarParseError as e:
    refused = e.code
refused.name                              # → "ILLEGAL_VALUE_TYPE"
```

A refusal is a `BovnarParseError` whose `code` is the one the reader gives the
same annotation in a document, so it tells the C function's three flags apart:

| `code` | C flags | Cause |
|---|---|---|
| `ILLEGAL_VALUE_TYPE` | `type_ok` false | An unknown family, an empty or repeated parameter, a parameter the family does not take (a base on `float_fix`, a width on `bool`), a width or base out of range |
| `UNIT_TOO_LONG` | `unit_ok` false, `unit_too_long` true | A unit parameter over 255 bytes |
| `UNIT_ILLEGAL`, `UNIT_PROFILE_UNKNOWN`, `UNIT_PROFILE_UNSUPPORTED` | `unit_ok` false | A unit parameter that is not a unit this build knows; the code is the one `unit_error_code()` gives it (§5.1) |

The reader's version gates are not applied: `datetime` (spec 1.1) and a profile
unit (spec 1.2) parse here whatever version a document declares. A `datetime`'s
epoch is no unit: its index is in `value_type.base` (`datetime_epoch_index`,
§19), as a `float_fix`'s q is.

**Validators.** Each returns a `bool`:

| Function | `True` for |
|---|---|
| `validate_identifier(s)` | A key: an ASCII letter, `_` or a character from U+00C0 on, then no space, control character, U+0080 to U+00BF, or punctuation spec §4.2 forbids (`-` and `+` are allowed). No length limit: the reader's `max_identifier_length` (255) is separate |
| `validate_symbol(s)` | A symbol, which is spelled as a key is |
| `validate_reference(s)` | A path such as `.a.b[0]`, without the `&`: identifier segments, each with optional `[digits]` indexes |
| `validate_number(s)` | A decimal number: an optional `-`, digits with at most one `.`, an optional `e`/`E` exponent. No `+`, and not `inf` |
| `is_special_number_string(s)` | Exactly `inf`, `ninf` or `nan` |
| `validate_digits_for_base(s, base)` | An integer literal: an optional `-` and digits of `base` (no sign at all in bases 64 and 85, where `+` and `-` are digits) |
| `validate_number_in_base(s, base)` | A quoted float literal: either sign, digits of `base` with at most one `.`, and an exponent marked `e` (bases up to 14) or `p` (the power-of-two bases up to 16) |
| `validate_uint_range(s, width, base=10)` / `validate_sint_range(s, width, base=10)` | A literal that fits `width` bits (0: any width); `inf`, `ninf` and `nan` pass |
| `validate_string(data)` | Bytes that are well-formed UTF-8 |

```python
literals.validate_identifier("max-speed")      # → True
literals.validate_identifier("max.speed")      # → False
literals.validate_reference(".axis[2].unit")   # → True
literals.validate_number("+1")                 # → False
literals.validate_digits_for_base("-ff", 16)   # → True
literals.validate_uint_range("256", 8)         # → False
literals.validate_sint_range("-80", 8, 16)     # → True
```

Three of them are narrower than their names. The range checks test the range
only, on text the digit check has already passed: `validate_uint_range("", 8)`
and `validate_uint_range(" 5", 8)` are `True`. `validate_digits_for_base` does
not check the base against the ones a type may declare: in base 1 the one digit
is `0`. And `validate_string` is the same test as `data.decode('utf-8')`; NUL and
the other control bytes the writer refuses in a string pass it.

**Digits.** `char_to_digit(ch, base)` is the value of one digit, `None` when it
is no digit of `base`: up to base 36 a letter's case does not matter, above it
`A` to `Z` are 36 to 61, and bases 64 and 85 use the Base64 and Ascii85
alphabets. `min_digits_for_type(vt)` is how many digits of the type's base the
largest value of an integer type of up to 64 bits takes; 0 for a width of 0 or
above 64, and a float type is answered as if it were an unsigned integer of its
width.

```python
from bovnar import make_type_spec, ValueTypeFamily

literals.char_to_digit("Z", 62)     # → 61
literals.char_to_digit("g", 16)     # → None
literals.min_digits_for_type(make_type_spec(ValueTypeFamily.UINT, 8, 16))   # → 2
```

**Parsing and formatting.** These are the functions the writer and the DOM use.
Each raises `BovnarArgumentError` where the C one returns `false` or -1:

| Function | Result |
|---|---|
| `parse_int64(s, vt)` / `parse_uint64(s, vt)` | The integer literal in the type's base; refused when it does not fit the type's width (64 when it declares none), and for a `+` or a space before it |
| `parse_double(s, vt)` / `parse_double_in_base(s, base)` | The float literal (base 10 or 16; `inf`, `ninf` and `nan`, not `-inf`) as the nearest double, rounded once from its exact value |
| `format_uint64(value, base=10, min_digits=0)` / `format_int64(...)` | The digits in base 2 to 62, 64 or 85, padded with zero digits to `min_digits` (which the C function caps at 64). A negative value has no form in bases 64 and 85 |
| `format_double(value, vt)` | The literal the writer emits under `vt`: the shortest digits that read back to the same double, in base 10 or 16 (any other base raises). A `float` width below 53 rounds first to a significand of that many bits: 16 for `float:16` and 32 for `float:32`, not IEEE binary16's 11 and binary32's 24 |
| `escape_repl_table()` | A `dict` of each escape letter to the character it stands for: `t`, `n`, `v`, `f`, `r`, `"` and `\`. The `\x` and `\u` escapes of spec 1.1 take digits and are not in it |

```python
u8 = make_type_spec(ValueTypeFamily.UINT, 8, 0)
f64 = make_type_spec(ValueTypeFamily.FLOAT, 64, 0)
literals.parse_uint64("255", u8)        # → 255
literals.format_double(0.1, f64)        # → "1e-1"
literals.parse_double("1e-1", f64)      # → 0.1
literals.format_uint64(5, 2, 8)         # → "00000101"
literals.escape_repl_table()["t"]       # → "\t"
```

> **`0x` in base 16.** In bases up to 36 the integer parsers are `strtoll` and
> `strtoull`, which take a `0x` prefix in base 16:
> `parse_uint64("0x10", make_type_spec(ValueTypeFamily.UINT, 64, 16))` is 16,
> though no document can carry that literal (the reader refuses `0x10`). Check
> with `validate_digits_for_base` first where that matters.

`bvn_looks_like_double` is not bound. It answers whether the text contains one
of `.`, `e`, `E`, `p` and `P`, so `deadbeef` looks like a float to it and `inf`
does not: a shortcut for the writer's own output, not a test of text from
anywhere else.

---

## 6. `Quantity`

`Quantity` is a typed, unit-annotated scalar value that preserves the original
text representation, type width, numeral base, and physical unit across a
`loads` / `dumps` round-trip.

```python
from bovnar import Quantity, ValueTypeSpec, ValueUnit
from bovnar.enums import ValueTypeFamily
```

### 6.1 Construction

`Quantity` is normally produced by `loads(..., typed=True)` rather than
constructed by hand, but direct construction is supported:

```python
from bovnar.structs import make_type_spec
from bovnar.enums   import ValueTypeFamily

vt = make_type_spec(ValueTypeFamily.FLOAT, 32)
q  = Quantity('101325.0', vt)          # dimensionless
q2 = Quantity('9.81',     vt, vu)      # vu is a ValueUnit, e.g. from parse_unit
```

For exact numeric input there is also the `Quantity.from_number` constructor,
which stores the value as an exact decimal literal (so writing it with `dumps`
is lossless to the format's precision):

```python
from decimal import Decimal
from fractions import Fraction

Quantity.from_number(Decimal('19.99'))                          # float_dec:64
Quantity.from_number(Decimal('1.1'), family=ValueTypeFamily.FLOAT, width=128)
Quantity.from_number(Fraction(837, 256),
                     family=ValueTypeFamily.FLOAT_FIX, width=32, frac=8)
```

It accepts a `Decimal`, `Fraction` (must be a terminating decimal), `int`,
`str` (a verbatim literal), or `float` (only as precise as the double), and
validates the width for the chosen family. A `str` must be a decimal number
literal in the format's own grammar — digits with an optional leading `-`,
`.` and exponent (`1.5`, `-2e-3`, `.5`), or `nan`, `inf`, `ninf` — so `''`,
`'abc'`, `' 1'` and `'+1'` raise `BovnarArgumentError` here rather than
failing later in `dumps`. For `float_dec` it also refuses a
finite value past the width's range — `Decimal('1e400')` does not fit
`float_dec:64` and would materialise to infinity — and a non-finite `float`
is stored under the format's own spelling (`inf`, `ninf`, `nan`).

### 6.2 Properties and methods

| Name | Type | Description |
|---|---|---|
| `q.raw` | `str \| None` | Original text token as it appeared in the BVNR stream |
| `q.vtype` | `ValueTypeSpec` | Type family, bit width, and numeral base |
| `q.unit` | `Unit` | Physical unit; `str(q.unit)` is its canonical text, `q.unit.raw` the `ValueUnit` struct (`BVN_UNIT_NONE` when dimensionless) |
| `q.value` | property | Decode `raw` to the closest native Python scalar (`int`, `float`, `str`, `bool`) — **lossy** for `float_dec` / `float_fix` / `float:128`+ (goes through a C `double`) |
| `q.unit_str` | `str` | Canonical unit string (e.g. `'m/s²'`), or `''` when dimensionless |
| `q.decimal` | `Decimal` | **Exact** value as `decimal.Decimal` from the verbatim literal — lossless at any width; raises for non-numeric families |
| `q.fraction` | `Fraction` | Exact value as `fractions.Fraction` (for `float_fix`, the exact `mantissa / 2**frac`); `None` for a typed null; raises for `nan`/`inf`/`ninf`, which have no ratio, and for non-numeric families. Its exact ratio holds 10^\|exponent\|, a number with that many digits, so it **refuses** (`BovnarArgumentError`) a literal whose decimal exponent — of its magnitude or of its last digit — exceeds `bovnar.quantity.FRACTION_EXPONENT_CAP` (100 000), or for `float_dec:256` its own range (611 937); `.decimal` is exact at any exponent |
| `q.fixed_point` | `(int, int)` | `(mantissa, frac_bits)` of a `float_fix` value; the value is `mantissa / 2**frac_bits`. `None` for a typed null; raises for `nan`/`inf`/`ninf`, which the reader accepts under `float_fix` but no Q-format datum holds |
| `q.stored_value` | `Decimal` | The value materialised into the declared IEEE/fixed format (round-to-nearest-even) — differs from `decimal` only when the literal carries more precision than the format holds. `None` for a typed null, as `decimal` is; `nan`/`inf`/`ninf` give the non-finite `Decimal` in every float family. A literal past the format's range (`1e-99999999`, `1e99999999`) gives the signed zero or infinity it rounds to, decided from the exponent without materialising the number; `ieee_bits` does the same |
| `q.ieee_bits` | `bytes` | IEEE-754 interchange bytes (binary16…256 for `float`, decimal16…256 for `float_dec`), little-endian word order |
| `q.epoch_name` | `str \| None` | For a `datetime`, the epoch name (`"unix"`, `"tai"`, …); `None` otherwise |
| `q.epoch_mjd` | `int \| None` | For a `datetime`, the epoch's Modified Julian Day; `None` otherwise |
| `q.datetime_fraction` | `str \| None` | For a `datetime` written as a literal with a fractional second, the verbatim sub-second digits (spec 1.1); `None` otherwise |
| `q.unit_source` | `str \| None` | The unit exactly as the document spelled it (library 2.0) — `ucum:mL{total}` where `q.unit` is `m~L`. Set by `loads(typed=True)`, or by the `unit_source=` constructor argument; `dumps()` writes it back, and refuses one that does not parse to `q.unit` — including a spelling on a `Quantity` that has no unit at all. `None` writes the canonical form |

### 6.3 Lossless numeric access (`float_dec`, `float_fix`, `float:128`/`256`)

`q.value` decodes through a C `double`, which loses precision for the
decimal-float, fixed-point, and wide binary-float families. The accessors above
instead use the verbatim literal text (and bovnar's arbitrary-precision
`bvn_float`), so the full precision the format carries is reachable from Python:

```python
q = bovnar.loads(b'.p=<float_dec:64> 3.141592653589793238462643383279503;',
                 typed=True)['p']
q.value          # 3.141592653589793   (lossy C double)
q.decimal      # Decimal('3.141592653589793238462643383279503')  (exact literal)
q.stored_value # Decimal('3.141592653589793')  (the decimal64-rounded value)

f = bovnar.loads(b'.x=<float_fix:32,q8> 3.27;', typed=True)['x']
f.fraction     # Fraction(837, 256)
f.fixed_point  # (837, 8)
```

These materialise over the **full** representable range, including exponents the
C parser cannot otherwise reach (beyond ~1e9865). `decimal` / `fraction`
work at every binary width bovnar allows (16, or a multiple of 32 up to 32768);
the bit-exact `stored_value` / `ieee_bits` apply to the IEEE encodings
(`float:16/32/64/128/256`), which are also exposed directly as `bovnar.BvnFloat`.

### 6.4 `dumps()` integration

`_emit_value` dispatches on `Quantity` before the plain `int` / `float` path,
so any dict that came from `loads(..., typed=True)` can be passed directly to
`dumps()` and will produce identical output:

```python
bvnr  = b".speed = <float:32,m/s> 9.81;"
doc   = bovnar.loads(bvnr, typed=True)
out   = bovnar.dumps(doc)
assert out.strip() == bvnr.strip()
```

The annotation is suppressed when `_needs_annotation` returns `False` — i.e.
when the type is `FLOAT:64` with no unit and no non-decimal base (matching the
C library's own default-annotation omission rules).

`dumps()` also accepts bare `decimal.Decimal` (written as an exact
`float_dec:64` literal) and terminating `fractions.Fraction` values directly —
a non-terminating fraction (e.g. `Fraction(1, 3)`) raises `BovnarArgumentError`,
as does a `Decimal` outside the range of `float_dec:64`. A list whose elements
are all `Decimal` / `Fraction` is written under one `<float_dec:64>`
annotation, so the elements mean what the scalars mean.

**Arrays of `Quantity`.** An array has one annotation. Elements that agree on
type and unit are written under it. Elements that agree on the unit but not
the type — or that carry no unit and differ in type, `[<uint:8> 1, <float:64>
2.5]` — are each written under their own annotation, as they were read.
Elements that carry *different* units have no valid form and raise.

**Octet streams.** `loads()` returns an octet stream as `bytes`, and `dumps()`
writes `bytes` / `bytearray` / `memoryview` back as one, so a document holding
one round-trips.

**What "can be passed directly to `dumps()`" does not promise** is that the
result passes the *document* tier: `dumps()` writes what `loads()` reads, and
`loads()` reads heterogeneous and ragged arrays that `dom_parse` and
`bovnar validate` refuse. See the note in
[§4.1](#41-high-level-api-loads--dumps).

---

## 7. `write_array` helper

`write_array` is a high-level helper exported from the top-level `bovnar`
namespace. It handles both flat and multi-dimensional arrays.

```python
from bovnar import write_array, Writer
from bovnar.structs import make_type_spec, make_unit_si
from bovnar.enums import ValueTypeFamily, BaseUnit

with Writer.to_mem() as w:
    # Single-row array
    write_array(w, "primes", [2, 3, 5, 7])

    # Multi-row array (rows separated by /)
    write_array(w, "matrix", [[1, 2, 3], [4, 5, 6]])

    # Typed array: whole-array type annotation
    vt = make_type_spec(ValueTypeFamily.UINT, 16)
    write_array(w, "ports", [80, 443, 8080], vt=vt)
```

Element types accepted per element: `int`, `float`, `str`, `bool`, `None`,
`bytes` (an octet stream), `dict` (written as a struct), or nested
`list`/`tuple` (written as a nested array). When *rows* is a flat sequence it
is treated as a single-row array; when all top-level elements are themselves
`list` or `tuple` it is treated as a multi-row array. Any other iterable (a
generator, a `range`) is materialised first.

**With `vt`, every element is held to it.** An `int` under an integer
annotation is checked against the width and written in the annotation's base —
quoted where the base requires it, so `255` under `<uint:16,_16>` is `"ff"`.
An element the annotation cannot describe — `80800` under `<uint:16>`, a
`float` or a `str` under an integer type, a `dict` under any — raises
`BovnarArgumentError` instead of producing a document nothing reads. `None` is
a null hole and is always allowed; a `Quantity` element carries its own
literal and is written as it is. `vu` takes a `ValueUnit` or a `Unit`.

---

## 8. pint bridge

bovnar units interoperate with [pint](https://pint.readthedocs.io/) through a
hand-verified translation table (`bovnar._pint_units`). pint is an **optional**
dependency, imported lazily on first use; importing `bovnar` never requires it.
Install with `pip install "bovnar[pint]"` (or `bovnar[all]`).

The four bridge functions are exported from the top-level `bovnar` namespace
(and from `bovnar._pint_bridge`):

| Function | Direction | Description |
|---|---|---|
| `to_pint(value, vu, *, ureg=None)` | bovnar → pint | Wrap a scalar/ndarray + `ValueUnit` in a pint `Quantity` |
| `to_pint_unit(vu, *, ureg=None)` | bovnar → pint | `ValueUnit` → pint `Unit` (dimensionless when no real unit) |
| `from_pint(qty, *, ureg=None, validate=True)` | pint → bovnar | `Quantity` → `(magnitude, ValueUnit)` |
| `from_pint_unit(unit, *, ureg=None, validate=True)` | pint → bovnar | `Unit` / `Quantity` / `str` → `ValueUnit` |

```python
import bovnar

vu  = bovnar.parse_unit("k~m")        # kilometre
qty = bovnar.to_pint(5.0, vu)         # <Quantity(5.0, 'kilometer')>

mag, vu2 = bovnar.from_pint(qty)      # (5.0, ValueUnit for km)
vu3      = bovnar.from_pint_unit("newton")     # str/Unit/Quantity all accepted
```

### 8.1 Prefixes ride in the unit, never the magnitude

bovnar's `k~m` maps to pint `kilometer` — the prefix is kept inside the unit
*name*, never folded into the magnitude. A wrapped value is therefore returned
unscaled, so a wrapped numpy array is never silently rescaled.

### 8.2 Affine temperature units

Offset/affine scales (`°C`, `°F`, Réaumur, Delisle, Newton, Rømer) cannot carry
a prefix or exponent — pint forbids it and bovnar never emits it. A prefixed or
exponentiated affine unit raises `BovnarArgumentError` rather than a cryptic
pint error.

`to_pint` / `to_pint_unit` take a `ValueUnit` or a `Unit`, so
`bovnar.to_pint(q.value, q.unit)` works on a typed value as it stands. A unit
pint cannot name raises `BovnarArgumentError`: the profile-only units, and
anything carrying the IEC prefixes `Ri~` / `Qi~`, which pint does not define.

### 8.3 Validation

`from_pint` / `from_pint_unit` validate the resulting `ValueUnit` by default
(`validate=True`); a pint unit that maps to a structurally invalid bovnar unit
(e.g. a prefix not permitted on that base) raises `BovnarArgumentError`. pint
units with more than `MAX_UNIT_COMPONENTS` (32) components, non-integer
exponents, or exponents outside `[EXPONENT_MIN, EXPONENT_MAX]` = `[-100, 100]`
also raise. Those two bounds are imported from `bovnar.structs` and
`bovnar.enums` rather than restated in the bridge: they were once written out as
8 and ±9, the values the C library had when the bridge was first written, and
stayed behind when it grew — so `from_pint` refused units `to_pint` had just
produced, and said the old numbers were bovnar's range.

### 8.4 Registry control: `build_registry`

A module-level default `pint.UnitRegistry` is built on first use. To share a
registry across calls — or to register bovnar's custom units onto your own —
pass `ureg=`:

```python
from bovnar._pint_units import build_registry, is_currency_unit, is_kind_scale

ureg = build_registry()                       # fresh registry with bovnar units
ureg = build_registry(my_existing_registry)   # extend an existing one
ureg = build_registry(with_currencies=False)  # skip the currency dimensions
ureg = build_registry(isolate_kinds=False)    # alias kinds onto pint's natives

qty  = bovnar.to_pint(5.0, vu, ureg=ureg)
```

`build_registry` registers bovnar's custom physical-unit definitions — units
where pint's own definition differs or is missing (e.g. `bvnr_gauss`,
`bvnr_var`, the historical German units) — plus, by default, the ISO 4217 +
crypto currencies as custom dimensions. `is_currency_unit(unit)` reports whether
a pint unit involves a currency dimension (holds for products such as
`USD/year`).

### 8.5 Quantity kinds: `isolate_kinds`

Bovnar tracks **quantity kinds** for units that carry no SI dimension yet are
not interchangeable — bit vs byte, an angle vs a plain count, `dB` vs `Np` vs
`pH`, one kind per turbidity method, and practical salinity. pint has no such
concept: to pint they are all dimensionless, so left to itself it will convert
an `NTU` into an `FNU`, a byte into 8 bits or a `pH` into a percentage —
precisely the answers Bovnar exists to withhold.

`build_registry` therefore gives **each kind its own pint dimension**, the same
device the currency table uses to stop `100 USD` becoming `100 EUR`. pint then
enforces Bovnar's rule natively:

```python
ureg = build_registry()                       # isolate_kinds=True by default
q    = bovnar.to_pint(4.2, bovnar.parse_unit("NTU"), ureg=ureg)

q.to("bvnr_fnu")        # pint.DimensionalityError — as bovnar refuses it
q.to("dimensionless")   # pint.DimensionalityError
q.to("bvnr_ntu")        # 4.2 — a conversion within the kind is fine
```

Conversions *within* a kind keep working, because that is what a shared
dimension means: `m~NTU → NTU`, `° → rad` (1 rad = 57.29578°), `sr = rad²`.
`bovnar._pint_units.is_kind_scale(unit)` reports whether a pint unit carries
one of these dimensions, as `is_currency_unit` does for money.

The cost is interoperability with pint's **native** units: a Bovnar-derived
byte no longer converts to pint's `megabyte`, nor a Bovnar radian to pint's
`degree`. If you need that more than you need the refusals:

```python
ureg = build_registry(isolate_kinds=False)    # alias onto pint's natives
```

which restores both the interop and pint's permissiveness. The default is
fidelity.

### 8.6 Semantic caveats

What survives kind isolation is recorded in
`bovnar._pint_units.SEMANTIC_CAVEATS`: `DECIBEL`, `NEPER` and `PH_SCALE`, whose
labels and refusals now match but whose *arithmetic* does not (pint will add two
decibels; 20 dB is a ratio of 100, not twice 10 dB), and `VAL`, where the
divalent water-analysis convention is a fact no unit system can infer. Consult
that dict when exact round-trip semantics matter.

The mapping is **not 1:1** by construction (bovnar `b`=bit vs pint barn,
`R`=roentgen vs pint's gas constant). The translation table is locked against
silent drift by `TestUnitTableIntegrity`, which re-derives every physical unit's
dimension and magnitude from bovnar and asserts the pint bridge reproduces both,
and by `TestPintAgreesWithBovnarOnWhatConverts`, which asserts that pint refuses
every conversion bovnar refuses — the invariant that was missing when pint was
quietly the more permissive of the two.

---

## 9. NumPy bridge

The NumPy bridge converts between bovnar arrays and `numpy.ndarray`. numpy is an
**optional** dependency, imported lazily on first use. Install with
`pip install "bovnar[numpy]"` (or `bovnar[all]`).

All five functions are exported from the top-level `bovnar` namespace (and from
`bovnar._numpy`):

| Function | Direction | Description |
|---|---|---|
| `to_numpy(src, *, dtype=None, return_unit=False)` | bovnar → numpy | Array → `ndarray` (optionally `(ndarray, unit_str)`) |
| `to_pint_array(src, *, dtype=None, ureg=None)` | bovnar → pint | Array → pint `Quantity` (ndarray data + unit) |
| `from_numpy(writer, key, arr, *, unit=None, float_format=None)` | numpy → bovnar | Write an `ndarray` into a `Writer` |
| `from_pint_array(writer, key, qty)` | pint → bovnar | Write a pint `Quantity` (magnitude + unit) into a `Writer` |
| `array_to_bvnr(key, arr, *, unit=None, pretty=True, float_format=None)` | numpy → bovnar | `ndarray` → bovnar bytes (convenience) |

### 9.1 Reading: `to_numpy`

*src* is either a `DomNode` for an ARRAY (random-access, from `dom_parse`) or
the nested list/tuple that `loads(..., typed=True)` produces. Both
`/`-separated rows and bracket nesting collapse to the same `ndarray` shape.

```python
import bovnar

# from a typed loads() result
doc = bovnar.loads(b'.a=<uint:8>[1,2,3];', typed=True)
arr = bovnar.to_numpy(doc['a'])               # dtype uint8

# with the whole-array unit
arr, unit = bovnar.to_numpy(
    bovnar.loads(b'.a=<float:32,m/s>[1,2,3]/[4,5,6];', typed=True)['a'],
    return_unit=True)                         # unit == 'm/s', arr.shape == (2, 3)

# from a DOM node
arr = bovnar.to_numpy(bovnar.dom_parse(b'.a=<sint:16>[10,-20,30];')['a'])
```

The bovnar `(family, width)` maps directly to the numpy dtype (`uint:8` →
`uint8`, `float:32` → `float32`, …). Pass `dtype=` to coerce.

The families with no exact native numpy dtype — `float_dec`, `float_fix`, and
the wide binary floats `float:128`/`256` (and any width above 64) — decode to
an **object array of exact `decimal.Decimal`** on the typed
(`loads(..., typed=True)`) path, which is lossless; pass `dtype='float64'` for
the lossy native conversion. The DOM (`dom_parse`) path only has a C `double`
for these, so it raises and points you at the typed path or `dtype='float64'`.
Integers wider than 64 bits need `dtype=object`, which gives an exact object
array of Python ints; without it `to_numpy` raises `BovnarArgumentError`.
An **empty** DOM array takes its dtype (and unit) from its annotation —
`<sint:32> []` is an `int32` array of shape `(0,)` — since it has no element to
infer one from; an empty array with no annotation is `float64`, as numpy
infers.

A **`datetime`** array on the **unix** epoch (spec 1.1) maps to/from
`datetime64[s]` — the integer epoch-seconds carrier is unix-relative, so it
round-trips losslessly. A non-unix epoch (`tai`, `gps`, …) is *not*
unix-relative, so `to_numpy` refuses it (mapping it to `datetime64` would
mis-date every value) and points you at `dtype='int64'` for the raw seconds.
On the write side a `datetime64[*]` array is coarsened to whole seconds and
written as `<datetime:64>` (the unix epoch is the default and is not
spelled out); `NaT` has no bovnar representation and is
rejected, and `array_to_bvnr` prepends the `#!bovnar 1.1` directive so its
output re-parses.

**Strict by default:**

* an array that mixes numeric encodings (e.g. `uint:8` and `float:64`) raises
  unless you pass `dtype=` to coerce to one;
* a Bovnar array is rectangular and homogeneous **at the document tier**
  (`dom_parse`, `bovnar validate`), so a `DomNode` always yields a regular
  ndarray. The streaming tier — `loads()`, and therefore `dumps()` — does not
  enforce that (see [§4.1](#41-high-level-api-loads--dumps)): a ragged typed
  list raises here unless you pass `dtype=object`;
* a `null` element cannot fill a dtype with no null slot — integer, `bool`, or
  string (which would otherwise silently become `0`/`False`/`'None'`); pass
  `dtype=float` (→ `NaN`) or `dtype=object` (→ `None`);
* the unit is a whole-array property (numpy has one dtype per array) — mixed
  units raise, as does mixing a dimensioned element with a dimensionless one;
  the unit is returned alongside the data, never baked into elements.

### 9.2 Writing: `from_numpy` / `array_to_bvnr`

```python
import numpy as np
import bovnar
from bovnar import Writer

with Writer.to_mem() as w:
    bovnar.from_numpy(w, "velocity",
                      np.array([1.5, 2.5], dtype=np.float32), unit="m/s")
out = w.get_output()

# one-shot convenience
raw = bovnar.array_to_bvnr("matrix", np.arange(8).reshape(2, 2, 2))

# exact decimal/fixed/wide-binary float arrays via float_format
from decimal import Decimal
prices = np.array([Decimal("19.99"), Decimal("0.01")], dtype=object)
bovnar.array_to_bvnr("p", prices, unit="$USD")            # -> <float_dec:64,$USD>
bovnar.array_to_bvnr("a", prices, float_format=("float", 128))   # binary128
```

* `from_numpy` requires a 1-D-or-higher array; write a 0-D scalar with the
  scalar `Writer` API instead.
* An empty axis is written as empty brackets, which keep no length for any axis
  after it, so only the **last** axis may be zero-length: shape `(2, 0)` is
  `[] / []` and reads back as `(2, 0)`, while `(0, 3)` would be written as `[]`
  and read back as `(0,)` — it raises `BovnarArgumentError` instead.
* *unit* may be a bovnar unit string, a `Unit`, a `ValueUnit`, or a pint
  `Unit`/`Quantity` (anything else raises `BovnarArgumentError`).
* Units apply to **numeric** arrays only — a unit on a `bool` or string array
  raises `BovnarArgumentError`.
* An **object array of `Decimal`/`Fraction`** is written as an exact
  decimal/fixed/wide-binary float. `float_format=(family, width[, frac])` —
  family `'float'`, `'float_dec'` (the default for a Decimal object array), or
  `'float_fix'` — selects the target.
* An **object array of Python `int`s** — what `to_numpy(..., dtype=object)`
  returns for integers wider than 64 bits — is written as integers, at the
  narrowest width that holds them and never under 64 (`<uint:71>[…]`).
* Any other object array is ambiguous and needs `float_format`.
* A **bytes array** (dtype kind `S`) is refused: decode it first
  (`arr.astype('U')`).
* A **masked array** with any masked entry is rejected (it would otherwise
  serialise the underlying value of a masked cell); fill or unmask it first.
* `array_to_bvnr` grows its write buffer like `dumps()` (4 MiB, doubling up to
  256 MiB).

### 9.3 pint arrays

`to_pint_array` and `from_pint_array` bridge straight through to pint
Quantities backed by ndarrays, reusing the unit translation above:

```python
q = bovnar.to_pint_array(
    bovnar.loads(b'.a=<float:64,k~m>[1,2,3];', typed=True)['a'])
# q is a pint Quantity: magnitude ndarray [1, 2, 3], unit 'kilometer'

with Writer.to_mem() as w:
    bovnar.from_pint_array(w, "dist", q)
```

A `datetime` array has no pint unit, so `to_pint_array` rejects it rather than
wrap it as a meaningless dimensionless quantity — use `to_numpy` for the
`datetime64` array (or `dtype='int64'` for the raw epoch seconds).

---

## 10. Currency helpers

The `bovnar.currency` submodule (exposed as `bovnar.currency`) provides
metadata for the ISO 4217 fiat and cryptocurrency `BaseUnit` members. It is pure
Python — no library or optional dependency required.

```python
from bovnar import currency, BaseUnit

currency.is_currency(BaseUnit.USD)   # True
currency.is_fiat(BaseUnit.USD)       # True
currency.is_crypto(BaseUnit.BTC)     # True

info = currency.currency_info(BaseUnit.USD)
info.code           # 'USD'
info.numeric_code   # 840 (ISO 4217 numeric; 0 for crypto)
info.minor_unit     # 2   (decimal places: 1 major = 10^minor minor units)
info.name           # 'US Dollar'

currency.minor_unit(BaseUnit.JPY)    # 0
currency.currency_code(BaseUnit.EUR) # 'EUR'
currency.from_code('GBP')            # BaseUnit.GBP

for ci in currency.all_fiat():       # all_crypto() / all_currencies() also exist
    ...
```

`CurrencyInfo` is a frozen dataclass; `currency_info`, `minor_unit`,
`currency_name`, `currency_code`, and `from_code` raise for non-currency bases
or unknown codes.

---

## 11. `Reader` reference

### 11.1 Construction

```python
with Reader() as r:
    ...
```

`Reader.__init__` calls `bvnr_reader_create` immediately.  Use as a context
manager or call `r.close()` explicitly to release the C object.

A reader is not re-entrant. While `read_mem`, `read_fd`, `read_file` or
`iter_mem` is running, the reader's own callbacks must not call `close()`,
start another read on it, or `set_unit_policy()`: each raises
`BovnarArgumentError("Reader is in use by its own callback")`, which aborts the
running parse and is re-raised from the read. Use a second `Reader` for a
nested parse, and close the first one after its read returns.

### 11.2 `read_mem`

`read_mem(data, *, on_verified, on_unverified, want_unit, on_error, **options)`

Parse BVNR from a `bytes`, `bytearray`, or `memoryview` object. The callbacks
are named parameters; every other keyword is a **read option**, one of the
names in `bovnar.READ_OPTIONS`. That table is the one list every reading entry
point takes its keywords from — `read_*`, `iter_mem`, `loads` / `load`, the
feeds (§4.7) and the `bovnar.stream` readers — so an option is never reachable
from one and missing from another. An unknown keyword is a `TypeError`, a value
outside its C field (a negative one, `max_string_length=70000`) a
`BovnarArgumentError`. Every option defaults to `0` / `False`.

| Parameter | Type | Default | Description |
|---|---|---|---|
| `data` | `bytes \| bytearray \| memoryview` | — | Input buffer |
| `on_verified` | `Callable[[Event, BvnrData \| None], bool] \| None` | `None` | Callback for validated events |
| `on_unverified` | `Callable[[Event, BvnrData \| None], bool] \| None` | `None` | Callback for raw pre-validation events |
| `on_error` | `Callable[[BovnarParseError], None] \| None` | `None` | Called for every error the reader reports, as a `BovnarParseError` with its code and position: under `continue_on_error`, each error it recovers from — reported nowhere else — and the error that ends a read, before the read raises it. Its return value is ignored; an exception it raises stops the read at the next event and is re-raised |
| `max_file_size` | `int` | `0` (unlimited) | Hard limit on bytes consumed |
| `continue_on_error` | `bool` | `False` | Enable resync mode |
| `strict_version` | `bool` | `False` | Reject a declared spec version newer than this build (`error_unsupported_spec_version`) |
| `text_only` | `bool` | `False` | Refuse a document that contains an octet stream (`error_octet_stream_forbidden`), for a channel that is not byte-transparent |
| `want_unit` | `Callable[[BvnrData], tuple \| None] \| None` | `None` | Optional read-time lossless unit/base conversion hook. Called per numeric value; return `(unit, base)` to request a conversion (the exact result arrives on the payload as `converted` and `converted_str()`, with `converted_rational()` / `converted_in_base()` beside it — see §4.2), or `None` to leave the value untouched. An inexact conversion aborts the parse (`error_unit_inexact`). |
| `want_unit_allow_nonterminating` | `bool` | `False` | Deliver an exact-rational-but-non-terminating conversion (e.g. km/h→m/s) as a rational — `converted_str()` returns `None`, `converted_rational()` the exact pair — instead of aborting with `error_unit_inexact` |
| `max_conversion_length` | `int` | `0` (default 1024) | Longest converted text a conversion may produce, in characters; a longer exact result aborts with `error_value_out_of_range` |
| `max_text_bytes` | `int` | `0` (default 2147483647) | Most bytes of text in the document (octet-stream payloads not counted); past it the read fails with `TEXT_DATA_TOO_LONG`. `stream.BVNR_NO_LIMIT` (2**64 − 1) lifts it |
| `max_array_items` | `int` | `0` (default 2147483647) | Most items in one array; past it the read fails with `TOO_MANY_ARRAY_ITEMS`. `stream.BVNR_NO_LIMIT` lifts it |
| `max_identifier_length` | `int` | `0` (default 255) | Longest key, in bytes (`IDENTIFIER_TOO_LONG`) |
| `max_string_length` | `int` | `0` (default 65535) | Longest string value, in bytes (`STRING_TOO_LONG`) |
| `max_number_length` | `int` | `0` (default 65535) | Longest number literal (`NUMBER_TOO_LONG`) |
| `max_symbol_length` | `int` | `0` (default 255) | Longest symbol (`SYMBOL_TOO_LONG`) |
| `max_reference_length` | `int` | `0` (default 65535) | Longest reference (`REFERENCE_TOO_LONG`) |
| `max_struct_nesting` | `int` | `0` (255) | Deepest struct nesting, at most 255 (`STRUCT_NESTING_TOO_HIGH`). `0` is the format's limit of 255, what the DOM and the CLI read at — not the C reader's own default of 64 |
| `max_array_nesting` | `int` | `0` (255) | Deepest array nesting, as `max_struct_nesting` (`ARRAY_NESTING_TOO_HIGH`) |

The five length caps and the two nesting caps are for untrusted input: lower
them to what the consumer can take. The length caps are the width of their C
field (`uint16_t`); a key or symbol longer than 255 bytes is accepted at a
higher cap, but `dumps()` and `bovnar validate` keep to the default.

### 11.3 `read_fd`

`read_fd(fd, *, …)` — the keyword parameters of `read_mem`, unchanged.

Parse BVNR from an open POSIX file descriptor. Parameters identical to
`read_mem` except the first argument is a non-negative `int` fd.

### 11.4 `read_file`

`read_file(path, *, …)` — the keyword parameters of `read_mem`, unchanged.

Convenience wrapper: opens `path` with `os.O_RDONLY`, calls `read_fd`, closes
the fd in a `finally` block. The `max_file_size` default is `MAX_FILESIZE_BYTES`
(16 MiB) rather than unlimited.

### 11.5 `iter_mem(data, *, verified_only, **options)`

Generator that collects all events from `read_mem` and yields
`EventPayload(event, raw, value_type, value_unit)` objects.

| Parameter | Default | Description |
|---|---|---|
| `verified_only` | `True` | When `False`, both callbacks fire and events may appear twice |
| `**options` | — | The read options, forwarded to `read_mem` |

`EventPayload` fields:

| Field | Type | Description |
|---|---|---|
| `event` | `Event` | The event code |
| `raw` | `bytes` | Raw token bytes |
| `value_type` | `ValueTypeSpec \| None` | Type annotation if present |
| `value_unit` | `ValueUnit \| None` | Unit if present |
| `text` | `str` (property) | `raw` decoded as UTF-8 (with `errors='replace'`) |
| `converted` | `bool` | `True` when a `want_unit` conversion was applied to this value |
| `converted_text` | `str \| None` | Exact converted value as a positional string; `None` if the conversion does not terminate in `converted_base` |
| `converted_base` | `int` | Base the converted text is rendered in |

### 11.6 Error-state properties

These properties query the C reader object after a failed parse. They describe
the error that **ended** the read. An error recovered under
`continue_on_error=True` does not appear here: after a read that succeeded,
`error_code` is `ErrorCode.NONE` and the positions are `0`, however many errors
were recovered, and `recovery_count` is how to tell that any were (spec §13.4).

| Property | Type | Description |
|---|---|---|
| `error_code` | `ErrorCode` | The error that ended the read; `NONE` after a successful one |
| `error_line` | `int` | 1-based line number of the error |
| `error_column` | `int` | 1-based column of the error |
| `error_offset` | `int` | Byte offset of the error |
| `error_byte` | `int` | The byte the error was raised on, `0` where it is not one byte's fault |
| `recovery_count` | `int` | Number of times resync was entered (incremented at error entry, not at resync completion) |

To see the errors a `continue_on_error` read recovered from, pass `on_error`
(§11.2): it is called with each one.

### 11.7 `MAX_FILESIZE_BYTES`

```python
from bovnar import MAX_FILESIZE_BYTES   # 16 * 1024 * 1024  (16 MiB)
```

Default `max_file_size` cap used by `Reader.read_file`.

### 11.8 `read_stream`

`read_stream(stream, *, …)` — the keyword parameters of `read_mem`, unchanged.

Parse a document pulled from a socket (`recv_into`) or a binary file object
(`readinto1`, `readinto` or `read`, in that order of preference; `readinto1`
hands over what a pipe or a socket file has, instead of waiting until the
reader's whole request is filled). A short read is fine, and an empty one is the
end of the document. The bytes are pulled as the parse needs them, so the
document is never held whole. An exception the object raises is re-raised from
`read_stream` unchanged. A text-mode file is refused, and so is a non-blocking
object with nothing to read yet: a pull cannot wait, so such a transport is fed
to a `DocumentFeed` instead (§4.7).

---

## 12. `Writer` reference

### 12.1 Construction class methods

| Method | Description |
|---|---|
| `Writer.to_mem(buf=None, cap=4194304, *, pretty=True, unit_flags=0, emit_version=False)` | Write to an in-process buffer. `buf` may be a pre-allocated `bytearray`; when `None` an internal buffer of size `cap` is allocated. |
| `Writer.to_fd(fd, *, pretty=True, unit_flags=0, emit_version=False)` | Write to an open POSIX file descriptor. |
| `Writer.to_file(path, *, pretty=True, unit_flags=0, emit_version=False)` | Open `path` for writing (`O_WRONLY\|O_CREAT\|O_TRUNC`, mode `0o644`) and write to it; the fd is closed when the writer is finished or destroyed. |
| `Writer.to_stream(stream, *, pretty=True, unit_flags=0, emit_version=False)` | Write to a socket (`sendall`) or a binary file object (`write`, repeated until it has taken every byte). Output is pushed a full 64 KiB buffer at a time and the rest at `flush()` or `finish()`, which then call the object's `flush` if it has one. An exception the object raises is re-raised from the write call that pushed. |

All four are used as context managers.  On clean exit (`exc_type is None`)
the context manager calls `finish()` automatically.

`unit_flags` takes [`UnitFlags`](#53-unitflags) — `REDUCE` rewrites each unit
and rescales its value, `ASCII_EXP` writes `^N` exponents, `NUM_CANONICAL`
keeps a rescaled float in float shape and writes annotations in one form. `emit_version=True` writes a leading
`#!bovnar M.N` naming the spec version this build implements, as
`write_version()` would. Every writer accepts structs and arrays nested up to
**255** deep, the same limit `Reader` and `loads()` read at.

### 12.2 Output retrieval

| Method / property | Description |
|---|---|
| `w.get_output() -> bytes` | Return the bytes written so far (mem writers only). |
| `w.bytes_written` | Number of bytes written (all writer modes). |
| `w.flush()` | Hand everything written so far to the sink now (`bvnr_write_flush`) and call the stream's `flush`, between any two writes; the bytes are the same as without it. A no-op for a memory writer and after `finish()`. |
| `w.finish()` | Flush and seal the output. Raises `BovnarWriteError` if any struct is still open. |
| `w.destroy()` | Release the underlying C writer object immediately. |

### 12.3 Scalar write methods

All scalar writers accept keyword-only unit parameters.  Unit resolution
priority: `unit_str` (parsed via `bvn_parse_unit_n`) → `unit_iec_base` →
`unit_si_base` → `BVN_UNIT_NONE` (no annotation emitted).

#### `write_uint(key, value, *, width=64, base=10, unit_str=None, unit_si_base=None, unit_si_prefix=SIPrefix.NONE, unit_si_exp=Exponent.LINEAR, unit_iec_base=None, unit_iec_prefix=IECPrefix.NONE)`

Write an unsigned integer. `base` selects the numeral system (10 or 16 for
inline values; any Bovnar-supported base for `write_bvni`).

#### `write_sint(key, value, *, width=64, base=10, unit_str=None, unit_si_base=None, unit_si_prefix=SIPrefix.NONE, unit_si_exp=Exponent.LINEAR)`

Write a signed integer. `base` 64 and 85 are **unsigned** — `+` and `-` are
digits in those alphabets — and raise `BovnarArgumentError` here and for a
signed `write_bvni`.

#### `write_float(key, value, *, width=64, unit_str=None, unit_si_base=None, unit_si_prefix=SIPrefix.NONE, unit_si_exp=Exponent.LINEAR)`

Write an IEEE 754 binary float.

#### `write_float_fix(key, value, *, width=64, q=0, unit_str=None, unit_si_base=None, unit_si_prefix=SIPrefix.NONE, unit_si_exp=Exponent.LINEAR)`

Write a Q-format fixed-point value. `q` is the number of fractional bits
(`0 ≤ q < width`).

#### `write_float_dec(key, value, *, width=64, unit_str=None, unit_si_base=None, unit_si_prefix=SIPrefix.NONE, unit_si_exp=Exponent.LINEAR)`

Write an IEEE 754-2008 decimal float.

#### `write_string(key, value)`

Write a bare quoted UTF-8 string with no type annotation:
```bovnar
.host = "localhost";
```
To emit an explicit `<utf8>` annotation, use the low-level `emit` API with
`Event.TYPE_ANNOTATION_START` / `TYPE_ANNOTATION_END` before `Event.DATA`.

#### `write_bool(key, value)`

Write a `bool` value (`token_is_bool`) — serialized as the bare keyword
`true` or `false`. On read-back it decodes to a Python `bool`.

#### `write_null(key)`

Write a null value (empty slot).

### 12.4 Extended integer writers

#### `write_bvni(key, value, *, width=64, base=10, signed=None, unit_str=None, unit_si_base=None, unit_si_prefix=SIPrefix.NONE, unit_si_exp=Exponent.LINEAR)`

Arbitrary-width integer writer that supports all Bovnar numeral bases (**2–62, 64, and 85**). Non-decimal values are formatted using Python's own
big-integer arithmetic and emitted as quoted strings.  `signed` defaults to
`True` when `value < 0`.

#### `write_bvnf_base(key, value_str, *, width=0, base=10, unit_str=None, unit_si_base=None, unit_si_prefix=SIPrefix.NONE, unit_si_exp=Exponent.LINEAR)`

Write a float from a pre-formatted string in base 10 or 16. `base` must be
10 or 16; any other value raises `BovnarArgumentError`.

### 12.5 Struct helpers

```python
w.begin_struct(key)   # emit ASSIGNMENT_START + STRUCT_START, increment depth
w.end_struct()        # emit STRUCT_END, decrement depth
```

`finish()` verifies that the struct depth is zero; an unclosed struct raises
`BovnarWriteError(GOT_INCOMPLETE_BVNR_STREAM)`.

### 12.6 Version directive

```python
w.write_version()                   # "#!bovnar 1.2": the spec this build implements
w.write_version(major=1, minor=1)   # or declare an older version explicitly
```

Emits a leading `#!bovnar <major>.<minor>` directive. With no arguments it
names the spec version this build implements (`bovnar.spec_version()`, 1.2) —
the same directive `emit_version=True` writes; pass `major` and `minor`
together to declare an older one. Must be called before any value is written;
calling it after output has begun raises `BovnarWriteError(INVALID_ARGUMENT)`.
Use it to self-declare the spec version a document targets (spec-1.1
constructs such as datetime literals need at least 1.1 to re-read, a
profile-notation unit such as `ucum:[IU]` needs 1.2).

### 12.7 Array helpers

```python
w.begin_array_row()   # emit ARRAY_ROW_START
w.end_array_row()     # emit ARRAY_ROW_END
w.new_array_dim()     # emit ARRAY_DIM_START (the / separator between rows)
```

### 12.8 Low-level `emit`

```python
w.emit(event, *, key=None, value=None, vt=None, vu=None)
```

Send an arbitrary event to the C writer. `key` and `value` are encoded as
UTF-8. When both `vt` and `value` are supplied the token type is inferred:
`_TOKEN_IS_STRING` for `ValueTypeFamily.UTF8`, and also for a `vt` whose base
is a radix other than 10 (`uint`, `sint` or `float` with `vt.base` not 0 or 10,
whose literal must be quoted); `_TOKEN_IS_NUMBER` otherwise. The base field of
`float_fix` (Q), `float_dec` and `datetime` (the epoch index) is not a radix and
never makes the value a string.

### 12.9 Octets, embedded documents and mux streams

```python
from bovnar import Writer, stream

w = Writer.to_mem(pretty=False)
w.write_uint("before", 1)
w.mux_begin("media")              # .media = <multiplexed octet stream>
w.mux_send(1, b"frame-1")         # one message on channel 1
w.mux_send(2, b"audio")
w.mux_end()
w.embed_document("inner", b".x = 5;")   # a document as an octet value
w.write_octets("raw", b"\x00\x01")       # any bytes-like object
w.write_uint("after", 2)
w.finish()
doc = w.get_output()
stream.mux_load(doc, key="media")       # → [(1, b'frame-1'), (2, b'audio')]
stream.parse_embedded(bovnar.loads(doc)["inner"])   # → {'x': 5}
```

`mux_begin`, `mux_send` and `mux_end` wrap `bvnr_mux_begin` / `_send` /
`_end`: the stream is one value of the document being written, so values may
come before it (read back before the first message) and after it, and a
document may carry more than one. The bytes of a stream are those `mux_dump`
writes. `embed_document` wraps `bvnr_embed_document`, embedding into the
document being written rather than making a document of one key, as
`stream.embed_document` does. A key is held to the rule of `dumps()`
(`BovnarArgumentError`), a channel to the unsigned 64-bit range. On a
`to_stream` writer, call `flush()` after `mux_send` for a consumer reading
live (§4.8).

---

## 13. DOM API

The DOM API parses a complete BVNR document into an in-memory tree for
random-access queries without writing a SAX callback.

### 13.1 `dom_parse(data) -> DomDoc`

Top-level convenience function (mirrors `DomDoc.parse`).

```python
import bovnar
doc = bovnar.dom_parse(bvnr_bytes)
```

### 13.2 `DomDoc`

Owning wrapper around `bvn_dom_doc_t`. Destroying the object frees the entire
tree; any `DomNode` derived from it becomes invalid after that point.

| Method / property | Description |
|---|---|
| `DomDoc.parse(data)` | Class method. Parse `bytes \| bytearray \| memoryview`. |
| `DomDoc.parse_fd(fd, policy=None, *, max_bytes=0)` | Class method. Parse from an open file descriptor. A read error raises the matching `OSError` (`EBADF` for a bad descriptor, `IsADirectoryError` for a directory, …), input over the cap `BovnarParseError(FILE_TOO_LONG)`. `max_bytes` lowers the cap of 256 MiB (`bvn_dom_parse_fd_ex`); `0`, or more than 256 MiB, keeps it. |
| `DomDoc.parse_file(path, policy=None, *, max_bytes=0)` | Class method. Open path and parse (fd closed in `finally`), under the cap of `parse_fd`. This ceiling is distinct from `Reader.read_file`'s `MAX_FILESIZE_BYTES` (16 MiB). Raises as `parse_fd` does, with the path on the `OSError`. |
| `doc.parse_error` | `ErrorCode` — `NONE` on success. |
| `doc[key]` | Return a `DomNode` by key; raises `KeyError` when absent. `key` goes to the path lookup, so `doc["server"]` is a top-level entry and a dotted or indexed path reaches below it: `doc["server.tls"]` is `doc["server"]["tls"]`, `doc["matrix[1][0]"]` an element. A key cannot contain `.` or `[`, so the two readings never collide. |
| `key in doc` | `True` when `doc[key]` would succeed — the same path rule. A non-`str` is never in a document. |
| `len(doc)` | Number of top-level entries. |
| `iter(doc)` | Iterate over `(key, DomNode)` pairs. |
| `doc.entries()` | Return all top-level `(key, DomNode)` pairs as a list. |
| `doc.lookup(path)` | Dot-separated path lookup, e.g. `'server.tls.cert'`. Returns `None` when absent. |
| `doc.to_dict()` | Convert entire document to a plain Python dict, dropping type and unit info. |
| `DomDoc.create()` | Class method. An empty document to build in (§13.5). |
| `doc.add(key, node)` | Add a `NewNode` as a top-level entry; returns it as a `DomNode` (§13.5). |

> **File-size limits:** `DomDoc.parse_fd` and `DomDoc.parse_file` apply an internal
> ceiling of `BVN_DOM_FD_MAX_BYTES` (256 MiB, `bovnar.dom.DOM_FD_MAX_BYTES`).
> This is 16× larger than `Reader.read_file`'s `MAX_FILESIZE_BYTES` (16 MiB).
> `max_bytes=` lowers it, as the C library's `bvn_dom_parse_fd_ex` does; there
> is no way to raise it, and no unlimited mode.
>
> **The DOM takes no read options.** The C DOM parse functions read with a
> fixed set (no total cap besides the one above, the 255-level nesting, and the
> reader's defaults for everything else), and neither binding can pass more:
> read with a `Reader` (§11) or `loads` for the limits of §11.2.
>
> **Building a tree.** `DomDoc.create()` and `doc.add(key, node)` are the C
> DOM's builder, for a tree made by hand from `NewNode`s (§13.5). Such a tree
> cannot be written out; build a document with `dumps()` or the `Writer`.
>
> **Errors:** the C fd functions return `NULL` for an allocation failure, a read
> error and input over the cap alike; the binding tells them apart by `errno`.
> A read error is the matching `OSError`, input over the cap is
> `BovnarParseError(FILE_TOO_LONG)`, and only an allocation failure is
> `MemoryError` — the one `DomDoc.parse` can raise, since the in-memory parse
> returns `NULL` for nothing else.

### 13.3 `DomNode`

Non-owning view into a `bvn_dom_node_t`. The parent `DomDoc` must remain alive
for as long as any derived `DomNode` is in use.

| Property / method | Description |
|---|---|
| `node.dom_type` | `DomType` enum value |
| `node.value_type` | `ValueTypeSpec` |
| `node.unit` | `ValueUnit` |
| `node.unit_str` | Unit as a canonical string via `bvn_dom_get_unit_string`, or `''` when the value carries no unit — the same contract as `Quantity.unit_str` (the C formatter's `no_unit` spelling is never returned) |
| `node.is_null()` | `True` for null values |
| `node.value_in_base_units()` | Numeric value scaled to SI base units (`float`); an affine unit gets its offset applied (`25 °C` → `298.15`). Returns `0.0` for *any* failure — non-numeric node, currency, affine-in-compound — which is indistinguishable from a genuine zero, and it rounds to `double`. Use `units.unit_to_si_factor()` (which reports `ok`) or the `want_unit` hook when either matters |
| `node.as_i64()` / `as_u64()` | Signed / unsigned 64-bit integer |
| `node.as_i32()` / `as_u32()` | Signed / unsigned 32-bit integer |
| `node.as_i16()` / `as_u16()` | Signed / unsigned 16-bit integer |
| `node.as_i8()` / `as_u8()` | Signed / unsigned 8-bit integer |
| `node.as_float()` | `float` (64-bit) |
| `node.as_bool()` | `bool` for `BOOL` nodes |
| `node.as_str()` | Python `str` for `STRING`, `SYMBOL`, or `REFERENCE` nodes |
| `node.as_bytes()` | `bytes` for `OCTET_STREAM` nodes |
| `node.as_int_str(base=10)` | Integer value as a string in the given base; result C string is freed before return |
| `node.datetime_fraction` | `str` of the verbatim ISO sub-second digits for a `datetime` written as a literal with a fraction (spec 1.1), else `None`; the carrier value is unchanged and still read via `to_python()` |
| `node.unit_source` | `str` of the unit exactly as the document spelled it (library 2.0), where that differs from the canonical form — `ucum:mL{total}` for a unit of `m~L`; `None` for a node with no unit, one already written canonically, or one a unit policy converted |
| `node[key]` | Child `DomNode` by string key (STRUCT) or integer index (ARRAY) |
| `key in node` | Membership test for STRUCT nodes (a direct member only; a non-`str` is never one) |
| `len(node)` | Element count for STRUCT or ARRAY nodes |
| `iter(node)` | For STRUCT: iterate `(key, DomNode)` pairs; for ARRAY: iterate elements |
| `node.entries()` | List of `(key, DomNode)` pairs (STRUCT nodes only) |
| `node.array_dims()` | Number of `/`-separated dimensions (ARRAY nodes only) |
| `node.to_python()` | Recursively convert to a native Python value (drops type/unit info). A multi-row array comes back as a list of rows, empty rows included (`[]/[]` → `[[], []]`), as `loads()` gives it |

### 13.4 `DomType`

```
NULL=0  INT=1  FLOAT=2  STRING=3  SYMBOL=4
REFERENCE=5  STRUCT=6  ARRAY=7  OCTET_STREAM=8  BOOL=9
```

### 13.5 Building a tree (`NewNode`)

`DomDoc.create()` makes an empty document and `NewNode` the nodes to put in it:
the C DOM's builder (`bvn_dom_doc_create`, `bvn_dom_node_alloc`,
`bvn_dom_node_from_*`, `bvn_dom_struct_add`, `bvn_dom_array_append`,
`bvn_dom_doc_add`, doc/08 §4.5). It is there for code that consumes a DOM, a
test fixture say, and it has the C builder's limits: it makes structs, arrays,
nulls and integers only, and a built tree **cannot be written out**, since the C
library has no DOM serialiser. To produce a document, use `dumps()` or the
`Writer`.

```python
from bovnar import DomDoc, NewNode

server = NewNode.struct()
server.add("port", NewNode.integer(8080, width=16))
server.add("tls", NewNode.null())
ports = NewNode.array()
ports.append(NewNode.integer(80, width=16))
ports.append(NewNode.integer(443, width=16))
server.add("ports", ports)

doc = DomDoc.create()
doc.add("server", server)
big = doc.add("big", NewNode.integer(2**100, width=128, unit="m"))
doc["server.port"].as_u16()     # → 8080
big.unit_str                    # → "m"
doc.to_dict()                   # → {"server": {"port": 8080, "tls": None, "ports": [80, 443]}, "big": 1267650600228229401496703205376}
```

| Constructor / method | Description |
|---|---|
| `DomDoc.create()` | An empty document; its `parse_error` is `NONE` |
| `doc.add(key, node)` | Add `node` as a top-level entry and return it as a `DomNode` of `doc`. A parsed document takes one too |
| `NewNode.null()` / `NewNode.struct()` / `NewNode.array()` | An empty node of that kind (`bvn_dom_node_alloc`) |
| `NewNode.integer(value, *, width=64, signed=None, unit=None)` | An integer node of type `sint:<width>` or `uint:<width>`; `signed=None` is `sint` for a negative value and `uint` otherwise, as for `write_bvni`. A width of 8, 16, 32 or 64 is `bvn_dom_node_from_i8` … `_u64`, which take no unit; a width of 65 to 32768 is `bvn_dom_node_from_bigint`, which takes `unit` (a unit string, `Unit` or `ValueUnit`). A value that does not fit the type, another width, or a unit on a narrow width raises `BovnarArgumentError` |
| `node.add(key, child)` | Add `child` as a member of a struct node (`bvn_dom_struct_add`) |
| `node.append(child)` | Append `child` to an array node (`bvn_dom_array_append`) |
| `node.dom_type` / `len(node)` | The node's `DomType`; the number of members or elements |

**Ownership.** The C attach functions take the node they are given on every
path: on success the container owns it, and on failure they destroy it. A
`NewNode` hands its node over once. After it is attached it is spent, and any
further use of it (adding it again, adding to it, reading it) raises
`BovnarArgumentError`: a node is in one tree at most, and a struct cannot be
filled after it was attached, so a tree is built from its leaves up. A refused
attach raises before anything is handed over and leaves the node with the
caller; it is refused for a key that is not an identifier (§5.8; the C functions
refuse only an empty one, but the path lookups could not reach another), for a
key the struct or document already has (which the C function would refuse by
destroying the node), for a spent node, for a node added to itself, and on the
wrong kind of container. Only an allocation failure inside the C call is a
`MemoryError` with the node gone. A `NewNode` collected before it is attached
frees its subtree.

**Reading it back.** A built tree reads through `DomDoc` and `DomNode` like a
parsed one, with two differences that come from the C builder: an integer node
of up to 64 bits carries no unit, and a built array records no row shape, so its
`array_dims()` is `0` and `to_python()` gives one flat list.

Not bound: `bvn_dom_node_alloc` of the other kinds (a float, string, symbol,
reference, bool or octet-stream node it makes holds no value and cannot be given
one), `bvn_dom_strdup` (a C allocation helper; the attach functions copy the key
themselves), and `bvn_dom_get_bigint` (a borrowed `bvn_int_t`; `as_int_str()`
and `to_python()` already read an integer of any width).

---

## 14. Running the test suite

```bash
# Run everything (tests marked needs_lib are skipped when libbvnr.so cannot be loaded)
pytest

# Run only the pure-Python tests (no library required)
pytest -m "not needs_lib"

# Run only integration tests (requires libbvnr.so)
pytest -m needs_lib

# Verbose with short tracebacks
pytest -v --tb=short
```

`needs_lib` is a registered marker (`pyproject.toml`); `conftest.py` turns it
into a skip when the library cannot be loaded, so a run without `libbvnr.so`
skips those tests rather than failing them. A test module whose every test
calls into the library sets `pytestmark = pytest.mark.needs_lib`.

The NumPy- and pint-bridge tests `pytest.importorskip` their dependency, so they
are skipped automatically when `numpy` / `pint` is not installed. Install
`bovnar[all]` to run the full suite.

---

## 15. Package layout

```
bovnar/
├── __init__.py      # loads() / dumps() / load() / dump() / dom_parse() / unit helpers — public API
├── _ffi.py          # ctypes FFI: library discovery + argtypes/restype
├── _fileio.py       # sockets and binary file objects as callback sources and sinks
├── _bigint.py       # int <-> digit text past CPython's 4300-digit int_max_str_digits limit
├── _bvnfloat.py     # BvnFloat: arbitrary-precision float + IEEE binary/decimal/fixed encoders
├── _numpy.py        # NumPy bridge: to_numpy/from_numpy/to_pint_array/array_to_bvnr (numpy optional)
├── _pint_bridge.py  # pint bridge: to_pint/from_pint/to_pint_unit/from_pint_unit (pint optional)
├── _pint_units.py   # verified bovnar↔pint unit table + build_registry()
├── currency.py      # ISO 4217 / crypto currency metadata (pure Python)
├── dom.py           # DomDoc, DomNode, DomType, NewNode — random-access DOM and its builder
├── enums.py         # Python IntEnum mirrors of C enums
├── exceptions.py    # BovnarError hierarchy
├── literals.py      # the C validators, literal parsers/formatters, parse_type_annotation
├── quantity.py      # Quantity — typed, unit-annotated scalar for lossless round-trips
├── reader.py        # Reader, EventPayload, UnitPolicy, UnitRule
├── stream.py        # framing, octet multiplexing, document-in-document, the feeds, the live writers
├── structs.py       # ctypes Structure/Union definitions + ValueUnitPrefix + make_* helpers
├── unit.py          # Unit — a ValueUnit with equality, hashing and its operations
├── units.py         # unit_to_si_factor, unit_convert_factor, etc.
└── writer.py        # Writer class

tests/
├── conftest.py                          # shared fixtures, needs_lib marker
├── test_abi.py                          # ctypes struct sizes/offsets against the C compiler
├── test_enums.py, test_structs.py       # the enum and struct mirrors
├── test_version.py                      # library / spec version reporting
├── test_reader.py, test_writer.py       # Reader and Writer
├── test_stream.py                       # framing, mux, document-in-document
├── test_stream_io.py                    # sockets and file objects as sources and sinks
├── test_stream_feed.py                  # DocumentFeed, FrameFeed, DemuxFeed
├── test_stream_vectors.py               # the shared wire vectors of doc/10 §9
├── test_stream_live.py                  # Writer.flush, MuxWriter, FrameWriter
├── test_dom.py, test_dom_vs_loads_parity.py   # the DOM, and its agreement with loads()
├── test_literals.py                     # bovnar.literals, the DOM builder, UnitConversion.ok
├── test_write_array.py, test_array_parser.py, test_array_roundtrip_fidelity.py
├── test_roundtrip_property.py, test_writers_agree.py, test_dumps_directive.py
├── test_escapes.py, test_unicode_normalisation.py
├── test_datetime.py, test_datetime_oracle.py
├── test_lossless_floats.py, test_hex_float_decode.py, test_decimal_tie_rounding.py
├── test_bigint_digits.py                # integers past the 4300-digit str limit
├── test_units.py, test_unit_object.py, test_unit_physics.py, test_unit_source.py
├── test_unit_policy.py, test_unit_factors_derived.py
├── test_base_rendering_property.py, test_convertible_implies_convertible.py
├── test_currency_units.py, test_currency_table_parity.py
├── test_numpy_bridge.py, test_example_numpy_roundtrip.py      # need numpy
├── test_pint_bridge.py, test_pint_factor_parity.py            # need pint
└── test_example_consistency.py          # the shipped examples

bench/
└── bench_stream.py                      # per-message cost of the fed demux and the live writer, run by hand
```

---

## 16. Error handling

All errors surface as subclasses of `BovnarError`. `bovnar.error_message(code)`
is the library's text for an `ErrorCode` (`bvn_error_to_string`):
`"string_too_long"` for `ErrorCode.STRING_TOO_LONG`.

| Exception | When raised |
|---|---|
| `BovnarLibraryNotFound` | `libbvnr.so` could not be loaded — raised on first use of the C library, not at `import bovnar`; the message lists every path tried |
| `BovnarParseError` | Parse error in `Reader` (carries `code`, `line`, `column`, `offset`, `byte`) |
| `BovnarWriteError` | Write error in `Writer` (carries `code`, `offset`) |
| `BovnarArgumentError` | Invalid argument passed to a helper (e.g. bad unit string, closed reader/writer) |
| `TypeError` | An unknown keyword where read options are taken (`max_string_lenght=`) |
| `OSError` | `DomDoc.parse_fd` / `parse_file` could not read the input (the `errno` subclass: `IsADirectoryError`, …); an over-cap input is `BovnarParseError(FILE_TOO_LONG)` instead |
| `MemoryError` | The C library could not allocate (a reader, writer, or DOM tree) |

**`unit_convert_factor` error semantics:** The C function uses the
`(ok, requires_affine)` pair to signal four distinct outcomes:

| ok | requires_affine | Meaning |
|---|---|---|
| `True` | `False` | Multiplicative conversion; `factor` is ready to use |
| `True` | `True` | Both sides are affine and share their zero (e.g. °C ↔ °Re, Réaumur), so `factor` alone is correct |
| `False` | `True` | Affine conversion the offsets do not cancel (e.g. °C ↔ K). **`factor` is `0.0`** — not a usable multiplier |
| `False` | `False` | Units are dimensionally incompatible → `BovnarArgumentError` |

`unit_convert_factor` raises `BovnarArgumentError` **only** when both
`ok=False` and `requires_affine=False`; the result carries the other three as
`c.ok` and `c.requires_affine`, so check `c.ok` before multiplying by `factor`
(`ok` is a field with a default of `True`, so `UnitConversion(f, affine)` built
by hand still works). When it is `False`, call
`convert_value`, which handles the two-step affine path, or call
`unit_to_si_factor` on both units and convert by hand. The `Unit` method
`convert_factor()` returns the bare number and therefore raises for the
`0.0` row instead of returning it.

**Callbacks returning `False`:** When an `on_verified` or `on_unverified`
callback returns `False`, the C parser stops and `bvnr_read` returns failure.
The Python layer then calls `_raise_error()` and raises `BovnarParseError` with
whatever error code the reader recorded.  `BovnarCallbackAbort` is defined in
`exceptions.py` but is not raised by the current implementation.

**Callbacks raising exceptions:** If the callback itself raises a Python
exception, that exception is captured, the C callback returns `False` to stop
the parse, and the original exception is re-raised from `read_mem` / `read_fd`
after the C call returns.

---

## 17. FFI details

### 17.1 `ON_ERROR_FUNC` signature

The error callback type matches the C `bvnr_on_error_fn` signature exactly:

```c
void (*bvnr_on_error_fn)(
    void*        userdata,
    error_code_t err,
    uint64_t     line,
    uint64_t     column,
    uint32_t     byte,
    uint64_t     offset);
```

In Python this is declared as:

```python
ON_ERROR_FUNC = ctypes.CFUNCTYPE(
    None,
    ctypes.c_void_p,   # userdata
    ctypes.c_int,      # error_code_t err
    ctypes.c_uint64,   # line
    ctypes.c_uint64,   # column
    ctypes.c_uint32,   # byte
    ctypes.c_uint64,   # offset
)
```

### 17.2 `BvnrWriteFlags` layout

`BvnrWriteFlags` mirrors the C `bvnr_write_flags_s` struct in full, including
its `unit_flags` field (`bvn_unit_flags_t`, a `uint32_t`) and the
`emit_version` flag and `_reserved[4]` words that follow it:

| Flag constant (C) | Value | Effect |
|---|---|---|
| `BVN_UNIT_FLAGS_NONE` | `0` | Default: full Unicode exponent characters |
| `BVN_UNIT_REDUCE` | `1 << 0` | Reduce compound units before serialising |
| `BVN_UNIT_ASCII_EXP` | `1 << 1` | Use `^N` ASCII exponent notation instead of Unicode superscripts |
| `BVN_NUM_CANONICAL` | `1 << 2` | Write a float that `BVN_UNIT_REDUCE` rescaled in float shape (`5000.0`, not `5000`), and every annotation in one form (width written out, default `_10` and `no_unit` left out) |

They are set from Python with the `unit_flags=` keyword of `Writer.to_mem` /
`to_fd` / `to_file`, as `UnitFlags` values; `emit_version=` sets the struct's
`emit_version` field. The constructors also set `max_struct_nesting` and
`max_array_nesting` to 255.

The writer respects the `unit_flags` stored inside the C writer object when
serialising unit annotations. `_emit_annotation` retrieves the live flags via
`bvnr_writer_unit_flags(w)` and passes them to `bvn_unit_to_string_ex`.

### 17.3 `write_string` behaviour

`Writer.write_string` emits a bare quoted string with no type annotation,
matching `bvnr_write_string` in the C library:

```bovnar
.host = "localhost";
```

To write a string with an explicit `<utf8>` annotation, use the low-level
`emit` API with `Event.TYPE_ANNOTATION_START` / `TYPE_ANNOTATION_END` events
before the `Event.DATA` event.

### 17.4 `_resolve_unit` default

When no unit arguments are supplied to `write_uint`, `write_sint`,
`write_float`, etc., the unit resolves to `BVN_UNIT_NONE` (zero components),
matching the C convenience helpers. No unit annotation is emitted in this
case. Passing `unit_si_base` or `unit_iec_base` produces a unit with one
component; the `no_unit` keyword is only produced when the
caller explicitly constructs a dimensionless `ValueUnit` with `BaseUnit.NONE`.

### 17.5 Signals inside a callback

Where C calls back into Python — the event callbacks of `Reader`, `mux_load`'s
per-message callback, `load_documents`, a parsing `FrameFeed`, the sources and
sinks of sockets and file objects — every exception the callback raises is kept,
the callback answers false so that C stops, and the exception is re-raised once
the C call has returned. One window is beyond the binding's reach: an exception
raised by a signal handler (`KeyboardInterrupt` from Ctrl-C) at the very entry
of a callback, before its body runs. ctypes reports it as "Exception ignored"
and the call goes on as if the callback had succeeded, so the interrupt is lost
and the callback's work for that event (a message, a pushed buffer) is missing.
Where an interrupt must stop the work reliably, use the pull-style feeds
(`DemuxFeed`, an unparsed `FrameFeed`), which make no Python callback per item,
or have the signal handler set a flag the loop checks.

---

## 18. `BaseUnit` enum

The `BaseUnit` enum mirrors the C `value_base_unit_e` for the two blocks of
the id space that have Python names: the native units (block 10) and the
currencies (block 90). See [doc/05 §11.1](05_bovnar_unit_system.md#111-enumerations) for the block layout — an id's leading
two digits name the vocabulary it comes from.

| Range | Members |
|---|---|
| 0 | `NONE` |
| 100000–100001 | `BIT`, `BYTE` |
| 100002–100008 | SI base units (`SECOND` … `CANDELA`) |
| 100009–100027 | Named SI derived units (`HERTZ` … `KATAL`) |
| 100028–100043 | Non-SI accepted units (`LITER` … `YEAR`) |
| 100044–100053 | Imperial/US length (`INCH` … `FATHOM`) |
| 100054–100061 | Imperial/US mass (`POUND` … `CARAT`) |
| 100062 | `FAHRENHEIT` |
| 100063–100066 | Pressure (`ATMOSPHERE` … `PSI`) |
| 100067–100070 | Energy (`CALORIE` … `THERM`) |
| 100071 | `HORSEPOWER` |
| 100072–100074 | Force (`POUND_FORCE`, `DYNE`, `KIP`) |
| 100075 | `KNOT` |
| 100076–100084 | US volume (`GALLON` … `BARREL`) |
| 100085–100086 | Area (`ACRE`, `BARN`) |
| 100087–100089 | Angle (`ARCMINUTE`, `ARCSECOND`, `GRAD`) |
| 100090–100097 | CGS units (`POISE` … `GALILEO`) |
| 100098–100100 | Radiation (`CURIE`, `ROENTGEN`, `REM`) |
| 100101–100102 | Logarithmic (`NEPER`, `DECIBEL`) |
| 100103 | `RANKINE` |
| 100104 | `SLUG` |
| 100105 | `THOU` |
| 100106–100108 | UK imperial volume (`PINT_UK`, `FLUID_OUNCE_UK`, `QUART_UK`) |
| 100109–100110 | Electrical power (`VAR`, `VOLT_AMPERE`) |
| 100111 | `KILOGRAM_FORCE` |
| 100112 | `INCH_HG` |
| 100113 | `RPM` |
| 100114 | `FOOT_POUND` |
| 100115–100116 | Mass additional (`DRAM`, `PENNYWEIGHT`) |
| 100117–100118 | Length additional (`CHAIN`, `ROD`) |
| 100119–100120 | Volume additional (`GILL`, `GILL_UK`) |
| 100121 | `STANDARD_GRAVITY` |
| 100122 | `METRIC_HORSEPOWER` |
| 100123 | `REVOLUTION` |
| 100124–100125 | Time additional (`MONTH`, `FORTNIGHT`) |
| 100126 | `ATMOSPHERE_TECHNICAL` |
| 100127–100128 | Textile linear density (`TEX`, `DENIER`) |
| 100129–100132 | Apothecary/dry volume (`FLUID_DRAM`, `MINIM`, `PECK`, `BUSHEL`) |
| 100133–100145 | Historical German units (`PFUND`, `ZENTNER`, `DOPPELZENTNER`, `LOT`, Prussian line/zoll/fuss/elle/rute, `KLAFTER`, `GERMAN_MILE`, `MORGEN`, `SCHEFFEL`) |
| 100146–100152 | Additional physical units (`SURVEY_FOOT`, `LEAGUE`, `CABLE`, `HAND`, `QUINTAL`, `SCRUPLE`, `BAUD`) |
| 100153–100156 | Temperature scales (`DELISLE`, `NEWTON_TEMP`, `REAUMUR`, `ROMER`) |
| 100157–100162 | Ratio/proportion units (`PERCENT`, `PER_MILLE`, `PER_MYRIAD`, `PER_CENT_MILLE`, `PPM`, `PPB`) |
| 100163 | `PH_SCALE` |
| 100164–100165 | Named speed units (`MILE_PER_HOUR`, `KILOMETER_PER_HOUR`) |
| 100166–100170 | Water hardness (`GERMAN_HARDNESS`, `ENGLISH_HARDNESS`, `FRENCH_HARDNESS`, `RUSSIAN_HARDNESS`, `AMERICAN_HARDNESS`) |
| 100171–100172 | Concentration (`VAL`, `GRAINS_PER_GALLON`) |
| 100173–100179 | Turbidity, salinity and conductivity (`TURBIDITY_NTU`, `TURBIDITY_FNU`, `PRACTICAL_SALINITY`, `CONDUCTIVITY_FACTOR`, `TURBIDITY_FTU`, `TURBIDITY_FAU`, `TURBIDITY_JTU`) |
| 100180–100185 | Temperature *differences* (`DELTA_KELVIN`, `DELTA_FAHRENHEIT`, `DELTA_DELISLE`, `DELTA_NEWTON_TEMP`, `DELTA_REAUMUR`, `DELTA_ROMER`) — `Δ°C` shares `DELTA_KELVIN` and `Δ°Ra` shares `DELTA_FAHRENHEIT` |
| 100186–100191 | The six units the profiles needed (`METER_WATER`, `CALORIE_IT`, `BTU_TH`, `TROY_POUND`, `APOTHECARY_DRAM`, `LONG_HUNDREDWEIGHT`) |
| 100192–100200 | US survey lengths (`SURVEY_INCH` … `SURVEY_ACRE`) |
| 100201–100203 | Typographic lengths (`POINT`, `PICA`, `LINE`) |
| 100204–100209 | US dry volumes and the trade measures (`DRY_GALLON`, `DRY_QUART`, `DRY_PINT`, `BOARD_FOOT`, `CORD`, `SURVEY_ACRE_FOOT`) |
| 100210–100214 | `DARCY`, `THERM_EC`, `REFRIGERATION_TON`, `DOBSON`, `SHAKE` |
| 100215–100216 | The ratios below ppb (`PPT`, `PPQ`) — spelled `pptr` and `ppq`; see "Why `pptr` and not `ppt`" in [doc/05 §3.25](05_bovnar_unit_system.md#325-ratio-and-proportion-units) |
| 100217–100225 | Nine units two or three vocabularies define (`PRINTERS_POINT`, `PRINTERS_PICA`, `HORSEPOWER_ELECTRIC`, `HORSEPOWER_BOILER`, `ABVOLT`, `ASSAY_TON`, `BUSHEL_UK`, `CLO`, `DEBYE`) |
| 100226–100236 | Calendar and sidereal time (`YEAR_TROPICAL`, `YEAR_GREGORIAN`, `YEAR_SIDEREAL`, `YEAR_COMMON`, `MONTH_SYNODAL`, `MONTH_TROPICAL`, `MONTH_SIDEREAL`, `DAY_SIDEREAL`, `HOUR_SIDEREAL`, `MINUTE_SIDEREAL`, `SECOND_SIDEREAL`) |
| 100237–100243 | CGS electrostatic (`STATVOLT`, `STATAMPERE`, `STATCOULOMB`, `STATFARAD`, `STATOHM`, `STATHENRY`, `STATSIEMENS`) |
| 100244–100246 | Luminance (`LAMBERT`, `APOSTILB`, `FOOTLAMBERT`) |
| 100247–100249 | Water-vapour permeance (`PERM_0C`, `PERM_23C`, `PERM_METRIC`) |
| 100250–100255 | Heat conventions (`CALORIE_MEAN`, `CALORIE_15C`, `CALORIE_20C`, `BTU_59F`, `BTU_60F`, `BTU_MEAN`) |
| 100256–100261 | `ELEMENTARY_CHARGE`, `SPERE`, `CIRCULAR_MIL`, `UNIT_POLE`, `HORSEPOWER_WATER`, `MONTH_GREGORIAN` |
| 100262–100263 | CGS electrostatic magnetics (`STATWEBER`, `STATTESLA`) |
| 900000–900165 | ISO 4217 fiat currencies (`AED` … `ZWL`), block 90 — see `CURRENCY_FIRST` |
| 900166–900215 | Cryptocurrencies (`BTC` … `RUNE`), the rest of block 90 — see `CURRENCY_LAST` |

There is **no `_SENTINEL`**. It named one past the highest member, which the C
tables were once indexed and bounded by; those tables are indexed by a dense
slot now, and over a blocked, sparse space a "one past the end" number would
only invite the bounds check it can no longer support. Use `UNIT_NATIVE_FIRST`
/ `UNIT_NATIVE_LAST` / `CURRENCY_FIRST` / `CURRENCY_LAST`, or ask
`bovnar.currency.is_currency`.

> **The profiles' opaque units are members too.** The C `value_base_unit_t`
> has a block per profile for the profile-only units — as shipped, UCUM's
> arbitrary atoms (200000–200039, `UCUM_IU` … `UCUM_IR`) and UN/ECE's package
> and count codes (300000–300024, `UNECE_ONE` … `UNECE_VIAL`), doc/11 §7.1 —
> and a parsed unit reports their ids, so `BaseUnit` names each one after its C
> enumerator, `bu_ucum_iu` → `BaseUnit.UCUM_IU`, as the Rust crate's
> `BaseUnit::UcumIu` does. (Until 2.0.0 it omitted them, and
> `ValueUnitComponent.base_unit` raised `ValueError` on `ucum:[IU]/mL`.) They
> have no native spelling and no dimension: `unit_to_str` returns the profile
> notation (`"unece:XBX"`), `unit_is_profile_only` is true, and pint has no
> form for them.

> **Note on `CUP`:** the Cuban-Peso currency is exposed as **`BaseUnit.CUP_`** (trailing
> underscore), not `BaseUnit.CUP`. The plain name `CUP` is the US-cup volume unit
> (enum value 100080); since Python enum member names must be unique, the currency
> at value 900033 takes the suffixed name. This affects the Python member name only — the
> `.bvnr` wire token is still the bare uppercase `CUP`, and case stays load-bearing
> (`cup` = volume, `CUP` = currency).

---

## 19. Spec 1.1 additions

These bindings target the **Bovnar spec 1.2**; spec 1.0 remains the frozen
baseline, and the package's own version (2.0.2) is a separate number
(`bovnar.version()` against `bovnar.spec_version()`). Spec 1.2's one addition,
the unit-profile notation, is covered in [§5.1](#51-the-unit-profile-notations-spec-12):
it is gated on a `#!bovnar 1.2` directive, which `dumps()` prepends when a
kept spelling or a profile-only unit needs it. The 1.1 features (all gated on a
`#!bovnar 1.1` directive — an unversioned document is treated as 1.0) are
exposed as:

- **Version:** `bovnar.version()` (library version string),
  `bovnar.version_number()` (the same as one int, `major * 10000 + minor * 100
  + patch`: 20100 for 2.1.0), `bovnar.spec_version()`
  → `(major, minor)` of the highest spec understood, and
  `bovnar.peek_version(data)` → the `(major, minor)` a document declares, or
  `None`. `Reader.declared_version` gives the same after a read; every reading
  entry point (`read_*`, `loads`, the feeds and stream readers) accepts
  `strict_version=True` to reject a too-new version
  (`ErrorCode.UNSUPPORTED_SPEC_VERSION`).
- **Richer escapes:** `\u{…}` and `\xHH` in string literals are decoded by the C
  reader, so `loads` returns the resulting text transparently. A surrogate /
  out-of-range `\u` is `ErrorCode.INVALID_CODEPOINT`.
- **Datetime family:** `loads` decodes a `<datetime:width,epoch>` value to a
  plain `int` (signed epoch seconds). With `typed=True` it is a `Quantity` whose
  `.epoch_name` (`"unix"`, `"tai"`, …) and `.epoch_mjd` recover the epoch;
  `bovnar.datetime_epoch_index(name)` goes the other way, to the index a
  datetime `ValueTypeSpec` holds in `base`
  (`make_type_spec(ValueTypeFamily.DATETIME, 64,
  bovnar.datetime_epoch_index("gps"))`), `None` for an unknown name.
  `dumps()` emits the `<datetime:…>` annotation and prepends `#!bovnar 1.1`
  automatically when the object contains a datetime. With `typed=True` the
  sub-second fraction of an ISO-8601 literal (spec 1.1) is preserved on the
  `Quantity` and re-emitted, so a `loads(typed=True)`→`dumps()` round-trip is
  lossless (`…T12:00:00.5Z` survives verbatim). The plain (non-typed) value is
  only the whole-second `int` carrier, so a non-typed `loads`→`dumps` drops the
  fraction; to read it explicitly use the DOM tier (`DomNode.datetime_fraction`)
  or the streaming reader (`bvnr_data_t.frac_data` via a callback).
  `ValueTypeFamily.DATETIME` is the family enum member.
- **Reference array indexing:** `&.matrix[0][1]` paths are stored verbatim and
  resolved by `DomDoc.lookup("matrix[0][1]")` at the DOM layer.

`ErrorCode` adds `INVALID_SPEC_VERSION` (42), `UNSUPPORTED_SPEC_VERSION` (43),
`INVALID_CODEPOINT` (44), `INVALID_DATETIME_LITERAL` (45), and
`DATETIME_LITERAL_UNSUPPORTED_EPOCH` (46).

---

## See also

- [Read & Write API](08_bovnar_readwrite_api.md) — the C reader, writer, and DOM these bindings wrap
- [Specification](03_bovnar_spec.md) — the format the package reads and writes
- [Unit & Currency Reference](05_bovnar_unit_system.md) — the unit model behind `Quantity` and the bridges
- [Tutorial](01_bovnar_tutorial.md) — the document syntax, by example
- [FAQ §13 — Python Bindings](02_bovnar_faq.md#13-python-bindings) — common questions about this package
- [Rust Bindings](14_bovnar_rust_bindings.md) — the same tiers from Rust

---

*End of Bovnar — Python Bindings (Bovnar spec 1.2).*
