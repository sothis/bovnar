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
// { ok: false, error: 38, error_name: 'unit_mismatch', tier: 'stream',
//   line: 1, column: ..., byte: ..., offset: ..., recovery_count: 0,
//   skipped_bytes: 0, declared_version: null }

bvnr.toJSON('.speed = 9.81 m/s;\n.name = "probe";');
// { ok: true, error: 0, error_name: 'none',
//   value: { speed: 9.81, name: 'probe' } }

bvnr.events('.x = <uint:16> 7;');
// { ok: true, events: [ { seq: 0, ev: 'stream_start', ... }, ... ] }

bvnr.version();  // "2.0.2"
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
| `validate(in, options?)` | `{ ok, error, error_name, tier, line, column, byte, offset, recovery_count, skipped_bytes, declared_version }` — the verdict of `bovnar validate`: stops at the first error |
| `errors(in, options?)` | `{ ok, error, error_name, line, column, byte, offset, recovery_count, skipped_bytes, declared_version, errors: [{ error, error_name, line, column, byte, offset, tier }] }` — reads on past each error and reports them all |
| `toJSON(in, options?)` | `{ ok, error, error_name, value }` — the DOM projected to plain JSON |
| `events(in, options?)` | `{ ok, error, error_name, line, column, byte, offset, recovery_count, skipped_bytes, declared_version, events: [...] }` — the reference event stream (streaming tier only) |
| `parse(in, options?)` | `{ ok, error, error_name, line, column, byte, offset, recovery_count, skipped_bytes, declared_version, events: [...], errors: [...] }` — `events` and `errors` in one pass (streaming tier only) |
| `eventsConvert(in, unit = '', base = 0, allowNonterminating = false, options?)` | as `events`; each numeric `data` event that can be delivered in `unit` also carries `converted`, `converted_base` and `converted_unit` |
| `doc(in, options?)` | `{ ok, error, error_name, tree: [{ key, value }, ...] }` — the document as a tree of keyed nodes |
| `version()` | library version string, e.g. `"2.0.2"` (`BVNR_VERSION_STRING`) |
| `versionNumber()` | the same as the monotonic integer `BVNR_VERSION` (2.0.2 → 20002) |
| `specVersion()` | `{ major, minor }` — the highest bovnar *spec* version this build understands (1.2) |
| `peekVersion(in)` | `{ major, minor }` or `null` — a document's leading `#!bovnar` directive, without parsing it |
| `errorName(code)` | the `error_name` a numeric result code maps to (`"none"` for 0) |
| `parseUnit(text)` | `{ ok, unit, error, error_name, profile_only }` — a unit's canonical spelling and why a string was refused |
| `unitsEqual(a, b)` | whether two unit strings denote the same unit; throws `TypeError` if either is not a unit |
| `unitsConvertible(a, b)` | `bvn_units_convertible`, the screen for a conversion target; a screen, not a guarantee |
| `convertUnit(value, from, to)` | `{ ok, value }` or `{ ok: false, error, error_name, value: null }` — one number through `bvn_unit_convert_value` (the **double** path; affine scales such as °C/°F included) |
| `unitProfiles()` | `{ ok, profiles: ["ucum", ...] }` — the compiled-in unit profiles |
| `createPolicy()` | a unit-policy builder (see below) |
| `Module` | the raw Emscripten module |

`options` is optional on every parsing surface and carries the C API's
reader-side settings:

```js
bvnr.validate(octets, { textOnly: true });
bvnr.validate(futureDoc, { strictVersion: true });
bvnr.events(doc, { maxFileSize: 16 * 1024 * 1024, maxArrayItems: 1e6 });
bvnr.events(doc, { policy: bvnr.createPolicy().requireUnit() });
```

| Option | Meaning (`bvnr_read_flags_t`) |
| ------ | ----------------------------- |
| `strictVersion` | a declared spec version newer than this build is `unsupported_spec_version`; a malformed directive is `invalid_spec_version`. Off by default, as in C |
| `textOnly` | refuse a document containing an octet stream (`octet_stream_forbidden`) — the assertion that a text-shaped pipeline can pass the document on unharmed |
| `maxFileSize` | cap on input bytes, octet payloads included; `0` = unlimited (the default) |
| `maxTextBytes` | cap on TEXT-scanner bytes (octet payloads do not count); `0` = the library default |
| `maxArrayItems` | cap on one array's item count; `0` = the library default |
| `maxConversionLength` | longest exact `converted` expansion a conversion may produce; `0` = 1024 |
| `maxIdentifierLength`, `maxStringLength`, `maxNumberLength`, `maxSymbolLength`, `maxReferenceLength` | per-token byte caps; `0` = the library default |
| `maxStructNesting`, `maxArrayNesting` | nesting limits; `0` = **255** (the format's hard limit, and what every WASM surface has always used) |
| `policy` | a unit policy from `createPolicy()` (see below) |

`toJSON` and `doc` accept `policy` only. The C DOM entry points take a unit
policy and no read flags, so passing `strictVersion`, `textOnly` or a limit
there **throws** rather than being silently ignored — the same reason
`bovnar query` refuses `--text-only`. An unknown option always throws (a typo
is not a setting). The limits are exact up to 2^53; pass a `BigInt` for
`maxFileSize` above that.

### Unit policy

`bvnr_reader_set_unit_policy` / `bvn_dom_parse_policy` — what the document
must contain, and what unit the consumer wants values delivered in. Units are
strings and every entry is checked by the library when it is added, so a bad
unit or a rule path that could never match throws at the call:

```js
const policy = bvnr.createPolicy()
  .requireUnit()                              // every numeric value needs a unit
  .target('m/s')                              // deliver what converts, converted
  .requireConvertibleTo('m')                  // every value must be a length
  .rule('.inlet.temperature', '°C')           // this field, converted
  .rule('.flow', 'm/s', { mode: 'require' })  // assert, deliver as written
  .normaliseSI()                              // everything else in SI base units
  .onInexact('leave');                        // unconvertible: deliver untouched

bvnr.validate(doc, { policy });
const r = bvnr.events(doc, { policy });       // data events carry `converted`
bvnr.toJSON(doc, { policy });                 // the DOM stores converted values
policy.dispose();
```

A rule that matches nothing is **satisfied** (the C contract); a value at a
rule's path that cannot be converted is `unit_mismatch`. `onInexact('leave')`
applies to policy-chosen targets, not to `rule`-named ones, which stay strict.
A policy may be used by any number of calls and lives until `dispose()`, after
which using it throws. See `doc/06_bovnar_unit_policy.md` for the full
semantics.

### Two tiers of validity

A Bovnar document is checked twice. The **streaming tier** is the reader: every
value's syntax, type, range and unit, reported with a line and column. The
**document tier** holds the rules no single value can break — the elements of an
array agree with one another, the records of an array share one shape, no scope
repeats a key — and is checked over the whole tree, so its errors
(`array_element_type_mismatch`, `array_row_size_mismatch`,
`struct_shape_mismatch`, `duplicate_struct_key`) have **no position**.

- `validate()` and `errors()` cover **both** tiers, and agree with `bovnar
  validate` on every document. A document-tier error has `tier: 'document'`
  and `line`, `column`, `byte` and `offset` of `0` — do not render that as
  position 0:0. `errors()` marks each entry's tier; `validate()` marks the one
  error it reports.
- `events()`, `parse()` and `eventsConvert()` are the reader's own view and
  cover the **streaming tier only**: their `ok` can be `true` for a document
  `validate()` refuses (`.a = 1; .a = 2;`). Use them to see what a document
  contains, not to decide whether it is valid.
- `toJSON()` and `doc()` go through the DOM, so `ok` covers both tiers too, but
  their result carries the error code alone: the DOM reports no tier and no
  position, exactly as `bovnar convert` does.

An internal failure — an options block this core does not understand, a policy
the library refuses, a refused conversion base, an allocation failure — is
neither tier: on `validate()` it is `tier: null` with
`error_invalid_argument`, and it has no position to place.

Where several errors are reported, `error` / `error_name` always name the
**first**, and the top-level `line`/`column`/`byte`/`offset` place it. An
allocation failure inside the module is reported in the same shape as any other
result — `{ ok: false, error: 28, error_name: 'invalid_argument', detail: 'out
of memory' }` — and as a thrown `Error('bovnar: out of memory')` only if even
that could not be allocated.

`recovery_count` says how often the resync-mode surfaces (`events`, `parse`,
`errors`, `eventsConvert`) had to recover; `skipped_bytes` says how many bytes
of the document that recovery discarded. A non-zero `skipped_bytes` means the
stream handed to you is **not** the whole document, which is the one thing no
callback can tell you: the skipped bytes were never parsed.

An event that carries a unit reports it twice: `unit` is the canonical form
(`m~L`), and `unit_source` is the unit exactly as the document spelled it
(`ucum:mL{total}`) — the only place a UCUM annotation or a profile's own
spelling survives. `unit_source` is absent where the value has no unit.

### Position and value caveats

- `line` is 1-based. `column` counts **characters**, not bytes, and a tab
  advances to the next multiple of 4 — unit symbols are routinely non-ASCII, so
  this differs often from a byte count. `offset` is the 0-based **byte** index
  into the input: use it to index the buffer. `byte` is the offending input
  byte's value, or 0 where the error is not attributable to one.
- On a `data` event, `base: 0` means "no explicit base was written", which is
  decimal 10 (`bvn_effective_base`).
- On a **datetime**, the type's `base` field is the **epoch index**, not a
  radix: 0 is unix, 1 is tai, and the epoch's name is beside it in `epoch`.
- Numbers in `text` are the source spelling, not a JS number. A wide integer's
  digits live only there (or in `converted_num`/`converted_den`).
- A `float_fix` value's `q` is its fixed-point scaling; a `type_q` parameter
  event carries it as `q`, never as `base`.

### `eventsConvert`: the exact rational

A conversion that is exact as a rational but has no finite expansion in the
output base (`100 km/h → m/s` is 250/9) arrives with `converted: null` when
`allowNonterminating` is set — and then `converted_num` / `converted_den` carry
the exact reduced rational as decimal **strings** (denominator > 0), the only
carriers the value has in that mode. They are omitted when `converted` is a
string; that text is already exact. An unparseable `unit` is
`unit_illegal`; a `base` that is neither 0 nor a base bovnar can write
(`2..62`, `64`, `85`) is `invalid_argument`, refused before the document is
read.

### `toJSON` projection notes

`toJSON` projects the validated DOM onto plain JSON *values*; it intentionally
drops unit/type metadata (use `events()` to recover that). Specifically:

- **Integers of up to 64 bits** are emitted as JSON number tokens carrying
  their exact decimal digits — lossless in the JSON text, but `JSON.parse`
  rounds anything above 2⁵³ to a double. **Integers wider than 64 bits** are
  emitted as JSON *strings* of decimal digits, as `bovnar convert` does, so the
  digits survive `JSON.parse` — and so a wide integer and a string are not
  distinguishable in `value`. Use `events()` when you need the exact digits
  together with the type.
- **A datetime written with sub-second digits** (`2026-06-15T12:00:00.5Z`) is
  **refused**: JSON could only carry its whole-second count, which is a
  different instant. The result is `ok: false`, `error_name:
  'value_out_of_range'`, with a `detail` string naming the top-level key —
  again matching `bovnar convert`. A whole-second datetime becomes its epoch
  count. `events()` and `doc()` deliver the fraction as `frac`.
- **Non-finite floats** (`inf`, `-inf`, `nan`) become JSON `null` — JSON has no
  native infinity/NaN, and this matches `bovnar convert`.
- **Octet streams** become a lowercase hex string.
- **Symbols** and **references** are emitted as their text.

### `doc`: the tree shape

`doc()` returns the DOM's own view, not a projection: every node carries
`familyName`, `width`, `base`, `unit`, `unitSource` and `epoch` (null where the
field does not apply), a scalar's number `text` beside its JSON `value`, a
datetime's sub-second digits as `frac`, a struct's `children`, an array's
`dims` and `rows`, and an octet stream's `total`/`chunks`. An integer wider
than 64 bits is a string in `value`, as in `toJSON`; unit/type metadata is
kept. This is what the web playground's parser shim uses to cross-check its
event-stream reconstruction.

## What is not exposed

This module is the **reader**. The C API's writer (typed `bvnr_write_*`
helpers, `bvnr_write_event`, the writer-side unit policy), the push-fed readers
(`bvnr_read_feed` / `bvnr_read_finish`), the `BVF1` frame codec, octet
multiplexing, the producer-side validation predicates (`bvn_validate_identifier`
and friends) and the `on_unverified` (pre-validation) event view have no
JavaScript surface here. Use the native library or the Python/Rust bindings for
those.


## License

MIT © Janos Sonntag

The `.wasm` binary embeds Bovnar's unit-profile tables, which carry identifier
strings from UCUM, QUDT, OM 2, UDUNITS-2, the CF standard name table and UN/ECE
Recommendations 20 and 21. Those identifiers belong to their publishers and the
MIT grant does not extend to them; the notices, per vocabulary, are in
[`THIRD_PARTY_NOTICES.md`](https://github.com/sothis/bovnar/blob/main/THIRD_PARTY_NOTICES.md).
Part 2 of the same file covers the Emscripten runtime code inside the generated
glue.
