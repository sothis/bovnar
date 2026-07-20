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

bvnr.version();  // "1.1.0"
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

| Function       | Returns                                                                    |
| -------------- | -------------------------------------------------------------------------- |
| `validate(in)` | `{ ok, error, error_name, line, column, byte, offset, recovery_count, declared_version }` |
| `toJSON(in)`   | `{ ok, error, error_name, value }` — DOM projected to plain JSON           |
| `events(in)`   | `{ ok, error, error_name, events: [...] }` — the reference event stream    |
| `version()`    | library version string, e.g. `"1.1.0"`                                     |

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
