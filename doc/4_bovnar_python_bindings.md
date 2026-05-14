# Bovnar Python Bindings

Pure-`ctypes` Python bindings for the **Bovnar (BVNR)** typed serialisation
library (spec v1.3 Working Draft).

No compiled extension module is needed — the bindings load `libbvnr_shared.so` at
import time via the standard `ctypes.CDLL` machinery.

---

## Requirements

| Requirement | Notes |
|---|---|
| Python ≥ 3.9 | `dataclasses`, `enum.IntEnum` |
| `libbvnr_shared.so` | Runtime only; see *Library discovery* below |
| pytest ≥ 7.4 | Test suite only (`pip install bovnar[dev]`) |

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

```python
from bovnar.reader import Reader
from bovnar import Event

def on_event(ctx, ev, data):
    if ev == Event.ASSIGNMENT_START:
        print("key:", data.raw_str())
    elif ev == Event.DATA:
        print("value bytes:", data.raw_bytes())
    return True   # returning False aborts parsing

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
vu = bovnar.parse_unit("k-g·m/s²")

# Convert a ValueUnit back to its canonical string
s = bovnar.unit_to_str(vu)     # → "k-g·m/s²"

# Scalar SI/IEC factor for a unit string
f = bovnar.unit_factor("M-Hz") # → 1_000_000.0
```

### Inline unit suffix

In addition to the unit embedded in a type annotation (`<float:64,m/s>`), the
Bovnar format supports an **inline unit suffix** placed directly after a scalar
value, before the terminating `;`:

```bovnar
.speed = 9.81 m/s;           # inline suffix, no annotation
.mass  = <float:64> 70.5 k-g;# annotation without unit, inline adopted
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
├── __init__.py      # loads() / dumps() / unit helpers — public API
├── _ffi.py          # ctypes FFI: library discovery + argtypes/restype
├── enums.py         # Python IntEnum mirrors of C enums (see note below)
├── exceptions.py    # BovnarError hierarchy
├── reader.py        # Reader class + EventPayload dataclass
├── structs.py       # ctypes Structure/Union definitions + make_* helpers
└── writer.py        # Writer class

tests/
├── conftest.py      # shared fixtures, needs_lib marker
├── test_enums.py    # pure-Python enum tests
├── test_structs.py  # pure-Python struct / helper tests
├── test_reader.py   # integration: Reader (needs_lib)
├── test_writer.py   # integration: Writer (needs_lib)
└── test_units.py    # mixed: unit parsing / serialisation (needs_lib for FFI)
```

---

## Error handling

All errors surface as subclasses of `BovnarError`:

| Exception | When raised |
|---|---|
| `BovnarLibraryNotFound` | `libbvnr_shared.so` not found at import |
| `BovnarParseError` | Parse error in `Reader` (carries `code`, `line`, `column`, `offset`) |
| `BovnarWriteError` | Write error in `Writer` (carries `code`, `offset`) |
| `BovnarCallbackAbort` | Event callback returned `False` |
| `BovnarArgumentError` | Invalid argument passed to a helper (e.g. bad unit string) |

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

| Flag constant | Value | Effect |
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
| 29–34 | Non-SI accepted units (`LITER` … `CELSIUS`) |
| 35–36 | `RADIAN`, `STERADIAN` |
| 37–38 | `TONNE`, `BAR` |
| 39–41 | `ELECTRONVOLT`, `DALTON`, `ASTRONOMICAL_UNIT` |
| 42–44 | `HECTARE`, `WEEK`, `YEAR` |

---

## Spec version

These bindings target **Bovnar spec v1.3 Working Draft**.  The
`ErrorCode.UNIT_MISMATCH` member (value 38) and
`ErrorCode.ARRAY_NESTING_TOO_HIGH` (value 6) are present in the current
implementation.  The reference C library implementation is under active
development; the FFI declarations in `_ffi.py` may need updating as the
ABI stabilises.
