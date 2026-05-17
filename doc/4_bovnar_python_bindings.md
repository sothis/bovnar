# Bovnar Python Bindings

> **Version:** 1.0

Pure-`ctypes` Python bindings for the **Bovnar (BVNR)** typed serialisation
library (spec v1.0).

No compiled extension module is needed — the bindings load `libbvnr_shared.so` at
import time via the standard `ctypes.CDLL` machinery.

---

## Requirements

| Requirement | Notes |
|---|---|
| Python ≥ 3.10 | `dataclasses`, `enum.IntEnum`, union-type annotations (`X \| Y`) |
| `libbvnr_shared.so` | Runtime only; see *Library discovery* below |
| pytest ≥ 7 | Test suite only (`pip install bovnar[dev]`) |

---

## Installation

```bash
# Editable install from source (recommended during development)
pip install -e ".[dev]"
```

---

## Library discovery

The bindings search for `libbvnr_shared.so` in this order:

1. **`LIBBOVNAR_PATH`** — absolute path to the `.so` file, e.g.
   ```bash
   export LIBBOVNAR_PATH=/opt/bovnar/lib/libbvnr_shared.so
   ```
2. **`LIBBOVNAR_DIR`** — directory that *contains* the `.so`, e.g.
   ```bash
   export LIBBOVNAR_DIR=/opt/bovnar/lib
   ```
3. **`ctypes.util.find_library('bovnar')`** — standard `ldconfig` / `LD_LIBRARY_PATH` search.
4. **In-tree build paths** — `../../build/`, `../../build/release/`, `../../`,
   `.` (resolved relative to the `_ffi.py` file; useful when building Bovnar
   alongside the bindings from a mono-repo).

If the library cannot be found a `BovnarLibraryNotFound` exception is raised
with the list of searched paths.

---

## Quick-start

### High-level API (`loads` / `dumps`)

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

### SAX-style streaming reader

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

### Generator / iterator interface

```python
from bovnar.reader import Reader

with Reader() as r:
    for payload in r.iter_mem(bvnr_bytes):
        print(payload.event, payload.text)
```

### DOM (random-access) API

```python
import bovnar

doc = bovnar.dom_parse(bvnr_bytes)

# Top-level key lookup
node = doc["sensor_id"]
print(node.as_i64())        # → 42
print(node.value_type)      # ValueTypeSpec(family=UINT, width=64, base=0)

# Struct traversal
cfg = doc["config"]
host = cfg["host"].as_str()

# Array access
arr = doc["values"]
for i in range(len(arr)):
    print(arr[i].as_float())

# Convert entire document to a plain Python dict (drops type/unit info)
d = doc.to_dict()
```

### Low-level writer

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

---

## Unit helpers

```python
import bovnar

# Parse a unit string into a ValueUnit struct
vu = bovnar.parse_unit("k~g·m/s²")

# Convert a ValueUnit back to its canonical string
s = bovnar.unit_to_str(vu)     # → "k~g·m/s²"

# Scalar SI/IEC prefix factor for a unit string
f = bovnar.unit_factor("M~Hz") # → 1_000_000.0
```

### Extended unit functions

The following functions operate on `ValueUnit` objects and are available both
from the top-level `bovnar` namespace and from `bovnar.units`.

```python
from bovnar import (
    unit_valid, unit_prefix_factor, unit_prefix_exponent,
    prefix_unit_valid,
    unit_to_si_factor, units_compatible,
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

# Check dimensional compatibility
ok = units_compatible(vu_a, vu_b)     # True if same SI dimension vector

# Conversion factor between two compatible units
c = unit_convert_factor(vu_from, vu_to)
# c.factor, c.requires_affine

# 7-element SI dimension exponent vector [m, kg, s, A, K, mol, cd]
dims = unit_dimension_vector(vu)       # e.g. [1, 0, -1, 0, 0, 0, 0] for m/s

# Reduce a compound unit to its canonical named SI unit
r = unit_reduce(vu)                   # r.unit, r.scale

# Convert a scalar value between units (handles affine conversions)
kelvin = convert_value(25.0, vu_celsius, vu_kelvin)

# Serialise with formatting options (see UnitFlags below)
s = unit_to_str_ex(vu, UnitFlags.ASCII_EXP)   # use ^N instead of Unicode superscripts
s = unit_to_str_ex(vu, UnitFlags.REDUCE)      # reduce to canonical named unit first
s = unit_to_str_ex(vu, UnitFlags.REDUCE | UnitFlags.ASCII_EXP)

# Exponent enum ↔ integer conversions
n   = exponent_to_int(Exponent.NEG_SQUARE)  # → -2
exp = int_to_exponent(-2)                    # → Exponent.NEG_SQUARE
```

`SI_DIM_NAMES` is the ordered tuple `('m', 'kg', 's', 'A', 'K', 'mol', 'cd')`
— the index positions used by `unit_dimension_vector`.

### `UnitFlags`

```python
from bovnar import UnitFlags   # also from bovnar.units

UnitFlags.NONE      # 0 — no special formatting
UnitFlags.REDUCE    # reduce to a canonical named SI unit before serialising
UnitFlags.ASCII_EXP # use ^N exponent notation instead of Unicode superscripts
```

`UnitFlags` is an `IntFlag` and its values may be OR-combined:

```python
s = unit_to_str_ex(vu, UnitFlags.REDUCE | UnitFlags.ASCII_EXP)
```

### `ValueUnitPrefix`

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

### Inline unit suffix

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

---

## `write_array` helper

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
`dict` (written as a struct), or nested `list`/`tuple` (written as a nested
array). When *rows* is a flat sequence it is treated as a single-row array;
when all top-level elements are themselves `list` or `tuple` it is treated as a
multi-row array.

---

## `Reader` reference

### Construction

```python
with Reader() as r:
    ...
```

`Reader.__init__` calls `bvnr_reader_create` immediately.  Use as a context
manager or call `r.close()` explicitly to release the C object.

### `read_mem(data, *, on_verified, on_unverified, max_file_size, continue_on_error)`

Parse BVNR from a `bytes`, `bytearray`, or `memoryview` object.

| Parameter | Type | Default | Description |
|---|---|---|---|
| `data` | `bytes \| bytearray \| memoryview` | — | Input buffer |
| `on_verified` | `Callable[[Event, BvnrData \| None], bool] \| None` | `None` | Callback for validated events |
| `on_unverified` | `Callable[[Event, BvnrData \| None], bool] \| None` | `None` | Callback for raw pre-validation events |
| `max_file_size` | `int` | `0` (unlimited) | Hard limit on bytes consumed |
| `continue_on_error` | `bool` | `False` | Enable resync mode |

### `read_fd(fd, *, on_verified, on_unverified, max_file_size, continue_on_error)`

Parse BVNR from an open POSIX file descriptor. Parameters identical to
`read_mem` except the first argument is a non-negative `int` fd.

### `read_file(path, *, on_verified, on_unverified, max_file_size, continue_on_error)`

Convenience wrapper: opens `path` with `os.O_RDONLY`, calls `read_fd`, closes
the fd in a `finally` block. The `max_file_size` default is `MAX_FILESIZE_BYTES`
(16 MiB) rather than unlimited.

### `iter_mem(data, *, verified_only, max_file_size)`

Generator that collects all events from `read_mem` and yields
`EventPayload(event, raw, value_type, value_unit)` objects.

| Parameter | Default | Description |
|---|---|---|
| `verified_only` | `True` | When `False`, both callbacks fire and events may appear twice |
| `max_file_size` | `0` | Forwarded to `read_mem` |

`EventPayload` fields:

| Field | Type | Description |
|---|---|---|
| `event` | `Event` | The event code |
| `raw` | `bytes` | Raw token bytes |
| `value_type` | `ValueTypeSpec \| None` | Type annotation if present |
| `value_unit` | `ValueUnit \| None` | Unit if present |
| `text` | `str` (property) | `raw` decoded as UTF-8 (with `errors='replace'`) |

### Error-state properties

These properties query the C reader object after a failed parse.

| Property | Type | Description |
|---|---|---|
| `error_code` | `ErrorCode` | Most recent error code |
| `error_line` | `int` | 1-based line number of the error |
| `error_column` | `int` | 1-based column of the error |
| `error_offset` | `int` | Byte offset of the error |
| `recovery_count` | `int` | Number of times resync was entered (incremented at error entry, not at resync completion) |

### `MAX_FILESIZE_BYTES`

```python
from bovnar import MAX_FILESIZE_BYTES   # 16 * 1024 * 1024  (16 MiB)
```

Default `max_file_size` cap used by `Reader.read_file`.

---

## `Writer` reference

### Construction class methods

| Method | Description |
|---|---|
| `Writer.to_mem(buf=None, cap=262144, *, pretty=True)` | Write to an in-process buffer. `buf` may be a pre-allocated `bytearray`; when `None` an internal buffer of size `cap` is allocated. |
| `Writer.to_fd(fd, *, pretty=True)` | Write to an open POSIX file descriptor. |
| `Writer.to_file(path, *, pretty=True)` | Open `path` for writing (`O_WRONLY\|O_CREAT\|O_TRUNC`, mode `0o644`) and write to it; the fd is closed when the writer is finished or destroyed. |

All three are used as context managers.  On clean exit (`exc_type is None`)
the context manager calls `finish()` automatically.

### Output retrieval

| Method / property | Description |
|---|---|
| `w.get_output() -> bytes` | Return the bytes written so far (mem writers only). |
| `w.bytes_written` | Number of bytes written (all writer modes). |
| `w.finish()` | Flush and seal the output. Raises `BovnarWriteError` if any struct is still open. |
| `w.destroy()` | Release the underlying C writer object immediately. |

### Scalar write methods

All scalar writers accept keyword-only unit parameters.  Unit resolution
priority: `unit_str` (parsed via `bvn_parse_unit_n`) → `unit_iec_base` →
`unit_si_base` → `BVN_UNIT_NONE` (no annotation emitted).

#### `write_uint(key, value, *, width=64, base=10, unit_str=None, unit_si_base=None, unit_si_prefix=SIPrefix.NONE, unit_si_exp=Exponent.LINEAR, unit_iec_base=None, unit_iec_prefix=IECPrefix.NONE)`

Write an unsigned integer. `base` selects the numeral system (10 or 16 for
inline values; any Bovnar-supported base for `write_bvni`).

#### `write_sint(key, value, *, width=64, base=10, unit_str=None, unit_si_base=None, unit_si_prefix=SIPrefix.NONE, unit_si_exp=Exponent.LINEAR)`

Write a signed integer.

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

Write the symbol `true` or `false` (no type annotation, no quotes).

#### `write_null(key)`

Write a null value (empty slot).

### Extended integer writers

#### `write_bvni(key, value, *, width=64, base=10, signed=None, unit_str=None, unit_si_base=None, unit_si_prefix=SIPrefix.NONE, unit_si_exp=Exponent.LINEAR)`

Arbitrary-width integer writer that supports all Bovnar numeral bases (2, 8,
10, 16, 36, 62, 64, 85). Non-decimal values are formatted using Python's own
big-integer arithmetic and emitted as quoted strings.  `signed` defaults to
`True` when `value < 0`.

#### `write_bvnf_base(key, value_str, *, width=0, base=10, unit_str=None, unit_si_base=None, unit_si_prefix=SIPrefix.NONE, unit_si_exp=Exponent.LINEAR)`

Write a float from a pre-formatted string in base 10 or 16. `base` must be
10 or 16; any other value raises `BovnarArgumentError`.

### Struct helpers

```python
w.begin_struct(key)   # emit ASSIGNMENT_START + STRUCT_START, increment depth
w.end_struct()        # emit STRUCT_END, decrement depth
```

`finish()` verifies that the struct depth is zero; an unclosed struct raises
`BovnarWriteError(GOT_INCOMPLETE_BVNR_STREAM)`.

### Array helpers

```python
w.begin_array_row()   # emit ARRAY_ROW_START
w.end_array_row()     # emit ARRAY_ROW_END
w.new_array_dim()     # emit ARRAY_DIM_START (the / separator between rows)
```

### Low-level `emit`

```python
w.emit(event, *, key=None, value=None, vt=None, vu=None)
```

Send an arbitrary event to the C writer. `key` and `value` are encoded as
UTF-8. When both `vt` and `value` are supplied the token type is inferred:
`_TOKEN_IS_STRING` for `ValueTypeFamily.UTF8`, `_TOKEN_IS_NUMBER` otherwise.

---

## DOM API

The DOM API parses a complete BVNR document into an in-memory tree for
random-access queries without writing a SAX callback.

### `dom_parse(data) -> DomDoc`

Top-level convenience function (mirrors `DomDoc.parse`).

```python
import bovnar
doc = bovnar.dom_parse(bvnr_bytes)
```

### `DomDoc`

Owning wrapper around `bvn_dom_doc_t`. Destroying the object frees the entire
tree; any `DomNode` derived from it becomes invalid after that point.

| Method / property | Description |
|---|---|
| `DomDoc.parse(data)` | Class method. Parse `bytes \| bytearray \| memoryview`. |
| `DomDoc.parse_fd(fd)` | Class method. Parse from an open file descriptor. |
| `DomDoc.parse_file(path)` | Class method. Open path and parse (fd closed in `finally`). |
| `doc.parse_error` | `ErrorCode` — `NONE` on success. |
| `doc[key]` | Return top-level `DomNode` by key; raises `KeyError` when absent. |
| `key in doc` | `True` when the top-level key exists. |
| `len(doc)` | Number of top-level entries. |
| `iter(doc)` | Iterate over `(key, DomNode)` pairs. |
| `doc.entries()` | Return all top-level `(key, DomNode)` pairs as a list. |
| `doc.lookup(path)` | Dot-separated path lookup, e.g. `'server.tls.cert'`. Returns `None` when absent. |
| `doc.to_dict()` | Convert entire document to a plain Python dict, dropping type and unit info. |

### `DomNode`

Non-owning view into a `bvn_dom_node_t`. The parent `DomDoc` must remain alive
for as long as any derived `DomNode` is in use.

| Property / method | Description |
|---|---|
| `node.dom_type` | `DomType` enum value |
| `node.value_type` | `ValueTypeSpec` |
| `node.unit` | `ValueUnit` |
| `node.unit_str` | Unit as a string via `bvn_dom_get_unit_string`, or `''` |
| `node.is_null()` | `True` for null values |
| `node.value_in_base_units()` | Numeric value scaled to SI base units (`float`) |
| `node.as_i64()` / `as_u64()` | Signed / unsigned 64-bit integer |
| `node.as_i32()` / `as_u32()` | Signed / unsigned 32-bit integer |
| `node.as_i16()` / `as_u16()` | Signed / unsigned 16-bit integer |
| `node.as_i8()` / `as_u8()` | Signed / unsigned 8-bit integer |
| `node.as_float()` | `float` (64-bit) |
| `node.as_str()` | Python `str` for `STRING`, `SYMBOL`, or `REFERENCE` nodes |
| `node.as_bytes()` | `bytes` for `OCTET_STREAM` nodes |
| `node.as_int_str(base=10)` | Integer value as a string in the given base; result C string is freed before return |
| `node[key]` | Child `DomNode` by string key (STRUCT) or integer index (ARRAY) |
| `key in node` | Membership test for STRUCT nodes |
| `len(node)` | Element count for STRUCT or ARRAY nodes |
| `iter(node)` | For STRUCT: iterate `(key, DomNode)` pairs; for ARRAY: iterate elements |
| `node.entries()` | List of `(key, DomNode)` pairs (STRUCT nodes only) |
| `node.array_dims()` | Number of `/`-separated dimensions (ARRAY nodes only) |
| `node.to_python()` | Recursively convert to a native Python value (drops type/unit info) |

### `DomType`

```
NULL=0  INT=1  FLOAT=2  STRING=3  SYMBOL=4
REFERENCE=5  STRUCT=6  ARRAY=7  OCTET_STREAM=8
```

---

## Running the test suite

```bash
# Run everything (library-dependent tests are skipped when libbvnr_shared.so is absent)
pytest

# Run only the pure-Python tests (no library required)
pytest -m "not needs_lib"

# Run only integration tests (requires libbvnr_shared.so)
pytest -m needs_lib

# Verbose with short tracebacks (already the default via pyproject.toml)
pytest -v --tb=short
```

---

## Package layout

```
bovnar/
├── __init__.py      # loads() / dumps() / dom_parse() / unit helpers — public API
├── _ffi.py          # ctypes FFI: library discovery + argtypes/restype
├── dom.py           # DomDoc, DomNode, DomType — random-access DOM
├── enums.py         # Python IntEnum mirrors of C enums
├── exceptions.py    # BovnarError hierarchy
├── reader.py        # Reader class + EventPayload dataclass
├── structs.py       # ctypes Structure/Union definitions + ValueUnitPrefix + make_* helpers
├── units.py         # unit_to_si_factor, unit_convert_factor, etc.
└── writer.py        # Writer class

tests/
├── conftest.py          # shared fixtures, needs_lib marker
├── test_analytics.py    # analytic / benchmarking tests (needs_lib)
├── test_array_parser.py # array parsing integration tests (needs_lib)
├── test_dom.py          # DOM API integration tests (needs_lib)
├── test_enums.py        # pure-Python enum tests
├── test_reader.py       # integration: Reader (needs_lib)
├── test_structs.py      # pure-Python struct / helper tests
├── test_unit_physics.py # unit physics / conversion tests (needs_lib)
├── test_units.py        # mixed: unit parsing / serialisation (needs_lib for FFI)
├── test_write_array.py  # write_array integration tests (needs_lib)
└── test_writer.py       # integration: Writer (needs_lib)
```

---

## Error handling

All errors surface as subclasses of `BovnarError`:

| Exception | When raised |
|---|---|
| `BovnarLibraryNotFound` | `libbvnr_shared.so` not found at import |
| `BovnarParseError` | Parse error in `Reader` (carries `code`, `line`, `column`, `offset`, `byte`) |
| `BovnarWriteError` | Write error in `Writer` (carries `code`, `offset`) |
| `BovnarArgumentError` | Invalid argument passed to a helper (e.g. bad unit string, closed reader/writer) |

**`unit_convert_factor` error semantics:** The C function uses the
`(ok, requires_affine)` pair to signal three distinct outcomes:

| ok | requires_affine | Meaning |
|---|---|---|
| `True` | `False` | Multiplicative conversion; `factor` is ready to use |
| `False` | `True` | Affine conversion required (e.g. °C ↔ K); `factor` is still valid but a plain multiply is insufficient |
| `False` | `False` | Units are dimensionally incompatible → `BovnarArgumentError` |

`BovnarArgumentError` is raised **only** when both `ok=False` and
`requires_affine=False`.  When `requires_affine=True`, call `convert_value`
which handles the two-step affine path automatically, or call `unit_to_si_factor`
on both units and perform the conversion manually.

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

## FFI details

### `ON_ERROR_FUNC` signature

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

### `BvnrWriteFlags` layout

`BvnrWriteFlags` mirrors the C `bvnr_write_flags_s` struct in full, including
the trailing `unit_flags` field (`bvn_unit_flags_t`, a `uint32_t`):

| Flag constant (C) | Value | Effect |
|---|---|---|
| `BVN_UNIT_FLAGS_NONE` | `0` | Default: full Unicode exponent characters |
| `BVN_UNIT_REDUCE` | `1 << 0` | Reduce compound units before serialising |
| `BVN_UNIT_ASCII_EXP` | `1 << 1` | Use `^N` ASCII exponent notation instead of Unicode superscripts |

The writer respects the `unit_flags` stored inside the C writer object when
serialising unit annotations. `_emit_annotation` retrieves the live flags via
`bvnr_writer_unit_flags(w)` and passes them to `bvn_unit_to_string_ex`.

### `write_string` behaviour

`Writer.write_string` emits a bare quoted string with no type annotation,
matching `bvnr_write_string` in the C library:

```bovnar
.host = "localhost";
```

To write a string with an explicit `<utf8>` annotation, use the low-level
`emit` API with `Event.TYPE_ANNOTATION_START` / `TYPE_ANNOTATION_END` events
before the `Event.DATA` event.

### `_resolve_unit` default

When no unit arguments are supplied to `write_uint`, `write_sint`,
`write_float`, etc., the unit resolves to `BVN_UNIT_NONE` (zero components),
matching the C convenience helpers. No unit annotation is emitted in this
case. Passing `unit_si_base` or `unit_iec_base` produces a unit with one
component; the `_SENTINEL`-only `no_unit` keyword is only produced when the
caller explicitly constructs a dimensionless `ValueUnit` with `BaseUnit.NONE`.

---

## `BaseUnit` enum

The `BaseUnit` enum mirrors the full C `value_base_unit_e`:

| Range | Members |
|---|---|
| 0 | `NONE` |
| 1–2 | `BIT`, `BYTE` |
| 3–9 | SI base units (`SECOND` … `CANDELA`) |
| 10–28 | Named SI derived units (`HERTZ` … `KATAL`) |
| 29–44 | Non-SI accepted units (`LITER` … `YEAR`) |
| 45–54 | Imperial/US length (`INCH` … `FATHOM`) |
| 55–62 | Imperial/US mass (`POUND` … `CARAT`) |
| 63 | `FAHRENHEIT` |
| 64–67 | Pressure (`ATMOSPHERE` … `PSI`) |
| 68–71 | Energy (`CALORIE` … `THERM`) |
| 72 | `HORSEPOWER` |
| 73–75 | Force (`POUND_FORCE`, `DYNE`, `KIP`) |
| 76 | `KNOT` |
| 77–85 | US volume (`GALLON` … `BARREL`) |
| 86–87 | Area (`ACRE`, `BARN`) |
| 88–90 | Angle (`ARCMINUTE`, `ARCSECOND`, `GRAD`) |
| 91–98 | CGS units (`POISE` … `GALILEO`) |
| 99–101 | Radiation (`CURIE`, `ROENTGEN`, `REM`) |
| 102–103 | Logarithmic (`NEPER`, `DECIBEL`) |
| 104 | `RANKINE` |
| 105 | `SLUG` |
| 106 | `THOU` |
| 107–109 | UK imperial volume (`PINT_UK`, `FLUID_OUNCE_UK`, `QUART_UK`) |
| 110–111 | Electrical power (`VAR`, `VOLT_AMPERE`) |
| 112 | `KILOGRAM_FORCE` |
| 113 | `INCH_HG` |
| 114 | `RPM` |
| 115 | `FOOT_POUND` |
| 116–117 | Mass additional (`DRAM`, `PENNYWEIGHT`) |
| 118–119 | Length additional (`CHAIN`, `ROD`) |
| 120–121 | Volume additional (`GILL`, `GILL_UK`) |
| 122 | `STANDARD_GRAVITY` |
| 123 | `METRIC_HORSEPOWER` |
| 124 | `REVOLUTION` |
| 125–126 | Time additional (`MONTH`, `FORTNIGHT`) |
| 127 | `ATMOSPHERE_TECHNICAL` |
| 128–129 | Textile linear density (`TEX`, `DENIER`) |
| 130–133 | Apothecary/dry volume (`FLUID_DRAM`, `MINIM`, `PECK`, `BUSHEL`) |
| 134 | `_SENTINEL` (internal bound; do not use) |

---

## Spec version

These bindings target **Bovnar spec v1.0**.  The
`ErrorCode.UNIT_MISMATCH` member (value 38) and
`ErrorCode.ARRAY_NESTING_TOO_HIGH` (value 6) are present in the current
implementation.  The reference C library implementation is under active
development; the FFI declarations in `_ffi.py` may need updating as the
ABI stabilises.

