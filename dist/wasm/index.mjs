// Bovnar WASM — ergonomic wrapper around the emscripten module.
//
// The C reference parser, compiled to WebAssembly. This IS the reference
// implementation: it synthesises default type annotations and performs full
// type/value/unit validation, so error_unit_mismatch and friends are reported
// exactly as the native `bovnar` CLI reports them.
//
// Usage:
//   import { loadBovnar } from 'bovnar-wasm';
//   const bvnr = await loadBovnar();
//   bvnr.validate('.v = <float:64,m/s> 9.81;');   // -> { ok, error, ... }
//   bvnr.toJSON('.v = 9.81 m/s;');                // -> { ok, value, ... }
//   bvnr.events('.v = 9.81 m/s;');                // -> { ok, events:[...] }
//   bvnr.eventsConvert('.v = 9.81 k~m/s;', 'm/s'); // -> events with `converted`
//   bvnr.version();                                // -> "1.1.0"
//
// `input` may be a string (UTF-8 encoded for you) or a Uint8Array (passed
// through verbatim — required if the document embeds octet streams with NULs).

import createBovnar from './bovnar.mjs';

const enc = new TextEncoder();

function toBytes(input) {
  if (input instanceof Uint8Array) return input;
  if (typeof input === 'string') return enc.encode(input);
  throw new TypeError('bovnar: input must be a string or Uint8Array');
}

export async function loadBovnar(opts) {
  const Module = await createBovnar(opts);

  // Call a `char* fn(const char* buf, int len)` export with a byte buffer,
  // then read back + free the returned NUL-terminated JSON string.
  function callJson(fnName, input) {
    const bytes = toBytes(input);
    const len = bytes.length;
    const inPtr = Module._malloc(len || 1);
    if (len) Module.HEAPU8.set(bytes, inPtr);
    let outPtr = 0;
    try {
      outPtr = Module.ccall(fnName, 'number', ['number', 'number'], [inPtr, len]);
      const json = outPtr ? Module.UTF8ToString(outPtr) : '';
      return JSON.parse(json);
    } finally {
      Module._free(inPtr);
      if (outPtr) Module.ccall('bvnr_wasm_free', 'void', ['number'], [outPtr]);
    }
  }

  // Same, for the conversion variant: (buf, len, unit, base, allow) where
  // `unit` is a NUL-terminated C string we allocate alongside the document.
  function callJsonConvert(input, unit, base, allowNonterminating) {
    const bytes = toBytes(input);
    const len = bytes.length;
    const unitBytes = enc.encode(unit || '');
    const inPtr = Module._malloc(len || 1);
    const uPtr = Module._malloc(unitBytes.length + 1);
    if (len) Module.HEAPU8.set(bytes, inPtr);
    Module.HEAPU8.set(unitBytes, uPtr);
    Module.HEAPU8[uPtr + unitBytes.length] = 0;
    let outPtr = 0;
    try {
      outPtr = Module.ccall(
        'bvnr_wasm_events_convert', 'number',
        ['number', 'number', 'number', 'number', 'number'],
        [inPtr, len, uPtr, base | 0, allowNonterminating ? 1 : 0]);
      const json = outPtr ? Module.UTF8ToString(outPtr) : '';
      return JSON.parse(json);
    } finally {
      Module._free(inPtr);
      Module._free(uPtr);
      if (outPtr) Module.ccall('bvnr_wasm_free', 'void', ['number'], [outPtr]);
    }
  }

  return {
    /** Validate a document (stops at the first error). Returns { ok, error,
     *  error_name, line, column, byte, offset, recovery_count,
     *  declared_version }. */
    validate: (input) => callJson('bvnr_wasm_validate', input),

    /** Validate in resync mode, collecting EVERY error. Returns
     *  { ok, errors: [{ error, error_name, line, column, byte, offset }],
     *  declared_version }. */
    errors: (input) => callJson('bvnr_wasm_errors', input),

    /** Parse and project the validated DOM to plain JSON. Returns
     *  { ok, error, error_name, value }. Unit/type metadata is dropped from
     *  `value`; use events() for that. */
    toJSON: (input) => callJson('bvnr_wasm_to_json', input),

    /** The reference (verified) event stream. Returns { ok, error, error_name,
     *  events: [{ seq, ev, tok, text?, family?, width?, base?, unit?, ... }] }. */
    events: (input) => callJson('bvnr_wasm_events', input),

    /** The event stream with the reader's LOSSLESS unit/base conversion armed,
     *  so numeric events also carry `converted` (the exact value, or null when
     *  it has no finite expansion in `base`), `converted_base` and
     *  `converted_unit`.
     *
     *  `unit` is a unit string ("m", "k~m/s"); pass '' to keep each value's own
     *  unit, which together with a `base` is a pure base conversion. `base` is
     *  the output base (0 keeps the value's own; otherwise 2..62, 64 or 85).
     *  With `allowNonterminating` a result that is exact as a rational but does
     *  not terminate in `base` arrives as `"converted": null` instead of being
     *  skipped. An unparseable `unit` comes back as error_unit_illegal.
     *
     *  Note: this surface runs in resync mode, so a value the conversion
     *  rejects is skipped along with its assignment rather than failing the
     *  call — it simply will not appear in `events`. */
    eventsConvert: (input, unit = '', base = 0, allowNonterminating = false) =>
      callJsonConvert(input, unit, base, allowNonterminating),

    /** Full document tree in document order, each value carrying its resolved
     *  type fields (familyName/width/base/unit/epoch). Returns
     *  { ok, tree: [{ key, value }] }. Used by the web playground's parser
     *  shim to reconstruct the tree + event-stream shapes. */
    doc: (input) => callJson('bvnr_wasm_doc', input),

    /** Library version string, e.g. "1.1.0". */
    version: () => Module.ccall('bvnr_wasm_version', 'string', [], []),

    /** The raw emscripten Module, for advanced use. */
    Module,
  };
}

export default loadBovnar;
