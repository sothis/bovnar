# bovnar

Python bindings for **Bovnar (BVNR)** — a typed, unit-aware serialisation format
with a C99 reference implementation.

In scientific and industrial systems the expensive failures are rarely bad
syntax — they are *unit confusion*: a value written in pounds-force and read as
newtons, feet read as metres. The number parses fine; the dimension is wrong.
Bovnar closes that gap: every value carries its own type family, bit-width,
numeric base, and **physical unit**, inline in the byte stream, validated by the
parser.

```bovnar
.velocity     = <float:64,m/s> 9.81;
.max_packet   = <uint:64,Mi~B> 16;
.price        = <float_dec:64,$USD> 19.99;
.matrix       = [1, 2, 3]/[4, 5, 6];
```

The wheel bundles the compiled `libbvnr` library, so there is nothing to
build and no system dependency to install — the bindings load it in-process via
`ctypes`.

## Install

```bash
pip install bovnar
```

Optional integrations:

```bash
pip install "bovnar[numpy]"   # numpy array bridge
pip install "bovnar[pint]"    # pint unit-registry bridge
pip install "bovnar[all]"     # both
```

## Usage

High-level dict API:

```python
import bovnar

doc = bovnar.dumps({
    "host": "api.example.com",
    "port": 443,
    "ratio": 3.5,
    "matrix": [[1, 2, 3], [4, 5, 6]],
})
print(doc.decode())

back = bovnar.loads(doc)
assert back["matrix"] == [[1, 2, 3], [4, 5, 6]]
```

Pass `typed=True` to `loads()` to preserve the exact type annotation and unit of
each value as a `Quantity`:

```python
data = bovnar.loads(b".velocity = <float:64,m/s> 9.81;\n", typed=True)
q = data["velocity"]
print(q.raw, bovnar.unit_to_str(q.unit))   # 9.81 m/s
```

Units and conversions are backed by the C library:

```python
m, mps = bovnar.parse_unit("m"), bovnar.parse_unit("m/s")
print(bovnar.units_compatible(m, mps))      # False
```

A streaming SAX-style `Reader`/`Writer` and a random-access `DomDoc` API are also
available — see the project documentation.

The `bovnar.stream` module adds multi-document framing, octet multiplexing, and
document-in-document:

```python
from bovnar import stream

blob = stream.dump_documents([{"id": 1}, {"id": 2}])     # frame many documents
docs = stream.load_documents(blob)                        # -> [{"id": 1}, {"id": 2}]

msg  = stream.mux_dump([(1, b"hello"), (42, b"world")])   # multiplex channels
print(stream.mux_load(msg))                               # [(1, b"hello"), (42, b"world")]
```

## Links

- Web: https://bovnar.io
- Source & docs: https://github.com/sothis/bovnar


## License

MIT — see [LICENSE](https://github.com/sothis/bovnar/blob/main/LICENSE).

The bundled `libbvnr` embeds Bovnar's unit-profile tables, which carry
identifier strings from UCUM, QUDT, OM 2, UDUNITS-2, the CF standard name table
and UN/ECE Recommendations 20 and 21. Those identifiers belong to their
publishers and the MIT grant does not extend to them; the notices ship in the
wheel and are also at
[`THIRD_PARTY_NOTICES.md`](https://github.com/sothis/bovnar/blob/main/THIRD_PARTY_NOTICES.md).
