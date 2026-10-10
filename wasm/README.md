# bovnar-wasm

The **Bovnar C reference parser, compiled to WebAssembly.**

This is the *actual* reference implementation: it synthesises default type
annotations and performs full type, value, and **physical-unit** validation. A
unit mismatch such as

```bovnar
.v = <float:64,m/s> 9.81 kg;
```

is reported as `error_unit_mismatch`, byte-for-byte as the native `bovnar` CLI
reports it — in the browser, in Node, in Deno, or on an edge runtime, with no
native build step.

## Install / build

The published package ships the prebuilt `.wasm` and glue. To rebuild from
source you need the [emscripten SDK](https://emscripten.org):

```sh
source ~/emsdk/emsdk_env.sh
./wasm/build_wasm.sh        # writes dist/wasm/
```

## Usage

```js
import { loadBovnar } from 'bovnar-wasm';

const bvnr = await loadBovnar();

bvnr.validate('.v = <float:64,m/s> 9.81 kg;');
// { ok: false, error: 38, error_name: 'unit_mismatch',
//   line: 1, column: ..., offset: ..., recovery_count: 0,
//   declared_version: null }

bvnr.toJSON('.speed = 9.81 m/s;\n.name = "probe";');
// { ok: true, error: 0, error_name: 'none',
//   value: { speed: 9.81, name: 'probe' } }

bvnr.events('.x = <uint:16> 7;');
// { ok: true, events: [ { seq: 0, ev: 'stream_start', ... }, ... ] }

bvnr.version();  // "2.0.0"
```

`input` is a string (UTF-8 encoded for you) or a `Uint8Array` (pass bytes
directly when the document embeds octet streams containing NUL bytes).

### Single-file build

`bovnar-wasm/single` inlines the WebAssembly as base64 in the JS, so there is no
second network/file fetch — handy for `file://`, strict CSP, or dropping into a
page with no bundler:

```js
import { loadBovnar } from 'bovnar-wasm/single';
```

## API surfaces

`loadBovnar()` resolves to an object with these members:

| Member | Returns |
| ------ | ------- |
| `validate(in)` | `{ ok, error, error_name, line, column, byte, offset, recovery_count, declared_version }` — stops at the first error |
| `errors(in)` | `{ ok, errors: [{ error, error_name, line, column, byte, offset }], declared_version }` — reads on past each error and reports them all |
| `toJSON(in)` | `{ ok, error, error_name, value }` — the DOM projected to plain JSON |
| `events(in)` | `{ ok, error, error_name, events: [...] }` — the reference event stream |
| `parse(in)` | `{ ok, error, error_name, events: [...], errors: [...], declared_version }` — `events` and `errors` in one pass |
| `eventsConvert(in, unit = '', base = 0, allowNonterminating = false)` | as `events`; each numeric `data` event that can be delivered in `unit` also carries `converted`, `converted_base` and `converted_unit` |
| `doc(in)` | `{ ok, tree: [{ key, value }, ...] }` — the document as a tree of keyed nodes |
| `version()` | library version string, e.g. `"2.0.0"` |
| `Module` | the raw Emscripten module |

An event that carries a unit reports it twice: `unit` is the canonical form
(`m~L`), and `unit_source` is the unit exactly as the document spelled it
(`ucum:mL{total}`) — the only place a UCUM annotation or a profile's own
spelling survives. `unit_source` is absent where the value has no unit.

### `toJSON` projection notes

`toJSON` projects the validated DOM onto plain JSON *values*; it intentionally
drops unit/type metadata (use `events()` to recover that). Specifically:

- **Big integers** are emitted as JSON number tokens carrying their exact
  decimal digits — lossless in the JSON text, but `JSON.parse` rounds them to a
  double. Use `events()` (or parse the raw text) when you need >2⁵³ precision.
- **Non-finite floats** (`inf`, `-inf`, `nan`) become JSON `null` — JSON has no
  native infinity/NaN, and this matches `bovnar convert`.
- **Octet streams** become a lowercase hex string.
- **Symbols** and **references** are emitted as their text.

## License

MIT © Janos Sonntag

The `.wasm` binary embeds Bovnar's unit-profile tables, which carry identifier
strings from UCUM, QUDT, OM 2, UDUNITS-2, the CF standard name table and UN/ECE
Recommendations 20 and 21. Those identifiers belong to their publishers and the
MIT grant does not extend to them; the notices, per vocabulary, are in
[`THIRD_PARTY_NOTICES.md`](https://github.com/sothis/bovnar/blob/main/THIRD_PARTY_NOTICES.md).
Part 2 of the same file covers the Emscripten runtime code inside the generated
glue.
