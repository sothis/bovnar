// Bovnar WASM — ergonomic wrapper around the emscripten module.
//
// SOURCE OF TRUTH: wasm/index.mjs. build_wasm.sh copies this file to
// web/bovnar_wasm.js (rewriting the core import to a cache-busted URL) and to
// dist/wasm/. Editing a copy appears to work and is silently reverted by the
// next build — that has already happened once, which is why
// bvnr_wasm_wrapper_sync now fails the suite when the two diverge.
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
//   bvnr.version();                                // -> "2.0.2"
//
// `input` may be a string (UTF-8 encoded for you), a Uint8Array or an
// ArrayBuffer (passed through verbatim — required if the document embeds octet
// streams with NULs, or if the bytes are not valid UTF-8: a JS string cannot
// carry a lone surrogate, and TextEncoder replaces one with U+FFFD).
//
// Every parsing surface also takes an optional options object carrying the
// reader-side C API: the bvnr_read_flags_t assertions and limits, and a unit
// policy. See "Reader options" below.

import createBovnar from './bovnar.mjs';

const enc = new TextEncoder();

// The brand on a policy object. A Symbol captured in this module's closure is
// unforgeable from outside: the public `_module` property this replaces could
// be copied onto any object, and the handle behind it then reached the core as
// a bare pointer into arbitrary memory.
const POLICY = Symbol('bovnar.policy');

// The { module, handle } behind a policy object, or null for anything that did
// not come from createPolicy().
function policyInfo(p) {
  const get = p != null && p[POLICY];
  return typeof get === 'function' ? get() : null;
}

// bvnr_wasm_opts_t, as laid out by the C compiler: four 32-bit fields, three
// 64-bit limits (8-aligned), six 32-bit limits, two 32-bit nesting fields and
// two reserved words. The version field makes a stale wrapper/core pair refuse
// the block instead of reading a layout it does not know.
const OPTS_SIZE = 72;
const OPTS_VERSION = 1;
const OPT_STRICT_VERSION = 0x1;
const OPT_TEXT_ONLY = 0x2;
const OPT_OCTETS_HEX = 0x4;

const READ_OPTIONS = [
  'strictVersion', 'textOnly',
  'maxFileSize', 'maxTextBytes', 'maxArrayItems', 'maxConversionLength',
  'maxIdentifierLength', 'maxStringLength', 'maxNumberLength',
  'maxSymbolLength', 'maxReferenceLength',
  'maxStructNesting', 'maxArrayNesting',
  'policy',
];
// `octets` only means something where a data event is delivered, so it is
// offered only on the event surfaces -- validate() and errors() have no events
// array, and accepting it there would be the silently-ignored option this
// module refuses everywhere else.
const EVENT_OPTIONS = [...READ_OPTIONS, 'octets'];
// The DOM entry points (bvn_dom_parse_policy) take a unit policy but no read
// flags. Accepting a flag here would be the "assertion the caller believes is
// in force but which is quietly ignored" that --text-only on `bovnar query`
// refuses, so these surfaces refuse it too.
const DOM_OPTIONS = ['policy'];

function toBytes(input) {
  if (input instanceof Uint8Array) return input;
  if (input instanceof ArrayBuffer) return new Uint8Array(input);
  if (typeof input === 'string') return enc.encode(input);
  throw new TypeError(
    'bovnar: input must be a string, Uint8Array or ArrayBuffer');
}

export async function loadBovnar(opts) {
  const Module = await createBovnar(opts);

  const hasExport = (name) => typeof Module['_' + name] === 'function';

  function malloc(n) {
    const p = Module._malloc(n);
    // emscripten's malloc RETURNS 0 on failure, it does not throw.
    if (!p) throw new Error('bovnar: out of memory');
    return p;
  }

  function needExport(name, what) {
    if (!hasExport(name))
      throw new Error(
        `bovnar: this WASM core does not export ${name}, so it is older ` +
        `than this wrapper and cannot take ${what}. Rebuild the module ` +
        `(source ~/emsdk/emsdk_env.sh && wasm/build_wasm.sh) and reload.`);
  }

  function readJson(ptr) {
    // A null result is the C side failing to allocate even its own error
    // object. JSON.parse('') turned that into "SyntaxError: Unexpected end of
    // JSON input", which names neither the cause nor this library.
    if (!ptr) throw new Error('bovnar: out of memory');
    return JSON.parse(Module.UTF8ToString(ptr));
  }

  // Call a `char* fn(const char* buf, int len)` export with a byte buffer,
  // then read back + free the returned NUL-terminated JSON string.
  function callJson(fnName, input) {
    const bytes = toBytes(input);
    const len = bytes.length;
    const inPtr = malloc(len || 1);
    // emscripten's malloc RETURNS 0 on failure, it does not throw. Without this
    // check a large paste that exhausts the heap would memcpy the input over
    // address 0 -- the null page and the module's static data -- silently
    // corrupting the parser for the rest of the session instead of failing.
    let outPtr = 0;
    try {
      if (len) Module.HEAPU8.set(bytes, inPtr);
      outPtr = Module.ccall(fnName, 'number', ['number', 'number'],
                            [inPtr, len]);
      // A null result is the C side failing to allocate even its own error
      // object. JSON.parse('') turned that into "SyntaxError: Unexpected end of
      // JSON input", which names neither the cause nor this library.
      return readJson(outPtr);
    } finally {
      Module._free(inPtr);
      if (outPtr) Module.ccall('bvnr_wasm_free', 'void', ['number'], [outPtr]);
    }
  }

  // Call a `char* fn(const char* text)` export with a NUL-terminated C string.
  // `what` names the argument in the NUL refusal ("a unit", "a type
  // annotation"), so a rejected string names the thing the caller passed.
  function callJsonCString(fnName, text, what) {
    const p = stringPtr(text, what || 'a unit');
    let outPtr = 0;
    try {
      outPtr = Module.ccall(fnName, 'number', ['number'], [p]);
      return readJson(outPtr);
    } finally {
      Module._free(p);
      if (outPtr) Module.ccall('bvnr_wasm_free', 'void', ['number'], [outPtr]);
    }
  }

  // Two NUL-terminated strings: the unit text and a profile namespace.
  function callJsonCString2(fnName, a, b) {
    const pa = stringPtr(a, 'a unit');
    let pb = 0, outPtr = 0;
    try {
      pb = stringPtr(b, 'a profile namespace');
      outPtr = Module.ccall(fnName, 'number',
                            ['number', 'number'], [pa, pb]);
      return readJson(outPtr);
    } finally {
      Module._free(pa);
      if (pb) Module._free(pb);
      if (outPtr) Module.ccall('bvnr_wasm_free', 'void', ['number'], [outPtr]);
    }
  }

  // Call a `char* fn(void)` export returning a JSON string.
  function callJsonNoArg(fnName) {
    const outPtr = Module.ccall(fnName, 'number', [], []);
    let out = 0;
    try {
      out = outPtr;
      return readJson(out);
    } finally {
      if (out) Module.ccall('bvnr_wasm_free', 'void', ['number'], [out]);
    }
  }

  // A NUL-terminated UTF-8 copy in the module's heap. The caller frees it.
  function stringPtr(text, what) {
    const s = String(text);
    if (s.includes('\0'))
      throw new TypeError(`bovnar: ${what} cannot contain NUL`);
    const bytes = enc.encode(s);
    const p = malloc(bytes.length + 1);
    try {
      Module.HEAPU8.set(bytes, p);
      Module.HEAPU8[p + bytes.length] = 0;
    } catch (e) {
      // A detached heap view would otherwise leak the block.
      Module._free(p);
      throw e;
    }
    return p;
  }

  // ---- reader options ------------------------------------------------------
  //
  // bvnr_read_flags_t, which had no way in from JavaScript: strict_version,
  // text_only, the byte/count caps, max_conversion_length and the length and
  // nesting limits. 0 means what it means in C for every numeric field (the
  // library's own default; for maxFileSize, unlimited), and the two nesting
  // fields default to 255 — the limit every WASM surface has always run at —
  // rather than to the reader's own default of 64.
  function u64(v, name) {
    // A number and a BigInt are both accepted; a string or a boolean is a typo,
    // not a limit. BigInt(number) is exact for an integer-valued double, so a
    // value above 2^53 keeps the digits the double actually holds.
    if (typeof v === 'bigint') {
      if (v < 0n || v > 0xffffffffffffffffn)
        throw new RangeError(`bovnar: ${name} must be an integer in 0..2^64-1`);
      return v;
    }
    if (typeof v !== 'number' || !Number.isInteger(v) || v < 0)
      throw new RangeError(`bovnar: ${name} must be an integer in 0..2^64-1`);
    const b = BigInt(v);
    if (b > 0xffffffffffffffffn)
      throw new RangeError(`bovnar: ${name} must be an integer in 0..2^64-1`);
    return b;
  }
  function uint(v, max, name) {
    if (typeof v !== 'number' || !Number.isInteger(v) || v < 0 || v > max)
      throw new RangeError(`bovnar: ${name} must be an integer in 0..${max}`);
    return v;
  }

  function optionWork(options, allowed, label) {
    if (options === undefined || options === null) return false;
    if (typeof options !== 'object' || Array.isArray(options))
      throw new TypeError(`bovnar: ${label}: options must be an object`);
    for (const k of Object.keys(options))
      if (!allowed.includes(k))
        throw new TypeError(`bovnar: ${label}: unknown option "${k}"`);
    // null means "not set", like undefined, for every option. Mixing the two
    // (null accepted by the limits, refused by the booleans) was a trap for
    // options built from JSON.
    for (const k of allowed)
      if (k !== 'policy' && options[k] != null) return true;
    return options.policy != null;
  }

  // Returns { ptr, policy }: a bvnr_wasm_opts_t in the module's heap (0 when
  // every field is at its default) and the policy handle (0 when none).
  function buildOptions(options, allowed, label) {
    if (options === undefined || options === null) return { ptr: 0, policy: 0 };
    let policy = 0;
    if (options.policy != null) {
      const info = policyInfo(options.policy);
      if (!info)
        throw new TypeError(
          `bovnar: ${label}: options.policy must come from createPolicy`);
      if (info.module !== Module)
        throw new TypeError(
          `bovnar: ${label}: the unit policy belongs to another loadBovnar ` +
          `instance; its handle points into a different WebAssembly memory`);
      policy = info.handle;
      if (!policy)
        throw new Error(`bovnar: ${label}: the unit policy has been disposed`);
    }
    const set = allowed.filter(k => k !== 'policy' && options[k] != null);
    if (!set.length) return { ptr: 0, policy };
    // Validate every value BEFORE allocating: a RangeError thrown after the
    // malloc would leak the block, and a caller retrying a bad option would
    // leak one per attempt.
    //
    // The two assertions are booleans, and used to be read for truthiness: a
    // `strictVersion: "false"` from a JSON config ENABLED strict mode, an
    // assertion the caller believed was off. Every other option is
    // type-checked; these are too.
    for (const k of ['strictVersion', 'textOnly']) {
      if (!allowed.includes(k)) continue;
      if (options[k] != null && typeof options[k] !== 'boolean')
        throw new TypeError(`bovnar: ${label}: ${k} must be a boolean`);
    }
    const octets = options.octets;
    if (octets != null && octets !== 'hex')
      throw new TypeError(
        `bovnar: ${label}: octets must be 'hex' (the only encoding offered)`);
    const fields = {
      maxFileSize: u64(options.maxFileSize ?? 0, 'maxFileSize'),
      maxTextBytes: u64(options.maxTextBytes ?? 0, 'maxTextBytes'),
      maxArrayItems: u64(options.maxArrayItems ?? 0, 'maxArrayItems'),
      maxConversionLength:
        uint(options.maxConversionLength ?? 0, 0xffffffff, 'maxConversionLength'),
      maxIdentifierLength:
        uint(options.maxIdentifierLength ?? 0, 0xffff, 'maxIdentifierLength'),
      maxStringLength:
        uint(options.maxStringLength ?? 0, 0xffff, 'maxStringLength'),
      maxNumberLength:
        uint(options.maxNumberLength ?? 0, 0xffff, 'maxNumberLength'),
      maxSymbolLength:
        uint(options.maxSymbolLength ?? 0, 0xffff, 'maxSymbolLength'),
      maxReferenceLength:
        uint(options.maxReferenceLength ?? 0, 0xffff, 'maxReferenceLength'),
      maxStructNesting:
        uint(options.maxStructNesting ?? 0, 255, 'maxStructNesting'),
      maxArrayNesting:
        uint(options.maxArrayNesting ?? 0, 255, 'maxArrayNesting'),
    };
    const ptr = malloc(OPTS_SIZE);
    // Re-read HEAPU8 after the malloc: growth swaps the ArrayBuffer and detaches
    // any view captured earlier.
    const dv = new DataView(Module.HEAPU8.buffer, ptr, OPTS_SIZE);
    dv.setUint32(0, OPTS_VERSION, true);
    let flags = 0;
    if (options.strictVersion) flags |= OPT_STRICT_VERSION;
    if (options.textOnly) flags |= OPT_TEXT_ONLY;
    if (octets === 'hex') flags |= OPT_OCTETS_HEX;
    dv.setUint32(4, flags, true);
    dv.setBigUint64(8, fields.maxFileSize, true);
    dv.setBigUint64(16, fields.maxTextBytes, true);
    dv.setBigUint64(24, fields.maxArrayItems, true);
    dv.setUint32(32, fields.maxConversionLength, true);
    dv.setUint32(36, fields.maxIdentifierLength, true);
    dv.setUint32(40, fields.maxStringLength, true);
    dv.setUint32(44, fields.maxNumberLength, true);
    dv.setUint32(48, fields.maxSymbolLength, true);
    dv.setUint32(52, fields.maxReferenceLength, true);
    dv.setUint32(56, fields.maxStructNesting, true);
    dv.setUint32(60, fields.maxArrayNesting, true);
    dv.setUint32(64, 0, true);
    dv.setUint32(68, 0, true);
    return { ptr, policy };
  }

  // The _ex twin of callJson. Called only when there is an option or a policy
  // to pass; the plain export stays the path for a wrapper paired with a core
  // that predates the _ex symbols.
  //
  // The DOM surfaces' _ex exports take THREE arguments -- (buf, len, policy) --
  // because bvn_dom_parse_policy takes a policy and no read flags; the reader
  // surfaces' take four (buf, len, opts, policy). `dom` says which.
  function callJsonOpts(plainFn, exFn, input, options, allowed, label, dom) {
    if (!optionWork(options, allowed, label))
      return callJson(plainFn, input);
    needExport(exFn, `${label} options`);
    const bytes = toBytes(input);
    const len = bytes.length;
    const o = buildOptions(options, allowed, label);
    let inPtr = 0, outPtr = 0;
    try {
      inPtr = malloc(len || 1);
      if (len) Module.HEAPU8.set(bytes, inPtr);
      outPtr = dom
        ? Module.ccall(exFn, 'number',
            ['number', 'number', 'number'], [inPtr, len, o.policy])
        : Module.ccall(exFn, 'number',
            ['number', 'number', 'number', 'number'],
            [inPtr, len, o.ptr, o.policy]);
      return readJson(outPtr);
    } finally {
      if (inPtr) Module._free(inPtr);
      if (o.ptr) Module._free(o.ptr);
      if (outPtr) Module.ccall('bvnr_wasm_free', 'void', ['number'], [outPtr]);
    }
  }

  // A base crosses as a C int. `base | 0` wrapped 4294967306 to 10 and turned
  // 2.5 into 2, each reported as a successful conversion; anything that is not
  // a non-negative integer the C side can hold goes over as -1, which it
  // refuses as error_invalid_argument. The C side additionally checks that the
  // value is a base bovnar can write (2..62, 64, 85, or 0 for "the value's
  // own"), so a document with no numeric value cannot slip an unusable base
  // past the reader.
  function wasmBase(base) {
    const b = Number(base);
    return Number.isInteger(b) && b >= 0 && b <= 0x7fffffff ? b : -1;
  }

  // (buf, len, unit, base, allow) where `unit` is a NUL-terminated C string we
  // allocate alongside the document; the _ex form appends options + policy.
  function callJsonConvert(input, unit, base, allowNonterminating, options) {
    // A boolean like strictVersion: a truthy string from a config used to arm
    // the relaxed (non-terminating) mode while reading as "off".
    if (typeof allowNonterminating !== 'boolean')
      throw new TypeError(
        'bovnar: eventsConvert: allowNonterminating must be a boolean');
    const withOptions = optionWork(options, EVENT_OPTIONS, 'eventsConvert');
    const fnName = withOptions
      ? 'bvnr_wasm_events_convert_ex' : 'bvnr_wasm_events_convert';
    if (withOptions)
      needExport('bvnr_wasm_events_convert_ex', 'eventsConvert options');
    const bytes = toBytes(input);
    const len = bytes.length;
    // The unit crosses as a NUL-terminated C string, so a NUL inside it ended it
    // early: 'm\0anything' converted to metres as if the rest had not been
    // written. No unit contains a NUL, so such a string is an unparseable unit
    // and must come back as one (error_unit_illegal, decided by the C side like
    // every other bad unit). Swapping the NUL for another control byte keeps
    // the string whole and just as unparseable.
    const unitBytes = enc.encode(String(unit || '').replace(/\0/g, '\x01'));
    const o = withOptions ? buildOptions(options, EVENT_OPTIONS, 'eventsConvert')
                          : { ptr: 0, policy: 0 };
    // Both allocations happen before the try, so a failure in the second used to
    // leak the first. Allocate, check for the null emscripten returns on OOM,
    // and let the finally below release whichever succeeded.
    let inPtr = 0, uPtr = 0, outPtr = 0;
    try {
      inPtr = malloc(len || 1);
      uPtr = malloc(unitBytes.length + 1);
      // Re-read HEAPU8 after every _malloc: growth swaps the ArrayBuffer and
      // detaches any view captured earlier.
      if (len) Module.HEAPU8.set(bytes, inPtr);
      Module.HEAPU8.set(unitBytes, uPtr);
      Module.HEAPU8[uPtr + unitBytes.length] = 0;
      const args = [inPtr, len, uPtr, wasmBase(base),
                    allowNonterminating ? 1 : 0];
      if (withOptions) args.push(o.ptr, o.policy);
      outPtr = Module.ccall(fnName, 'number',
        args.map(() => 'number'), args);
      return readJson(outPtr);
    } finally {
      if (inPtr) Module._free(inPtr);
      if (uPtr) Module._free(uPtr);
      if (o.ptr) Module._free(o.ptr);
      if (outPtr) Module.ccall('bvnr_wasm_free', 'void', ['number'], [outPtr]);
    }
  }

  // ---- unit policy ---------------------------------------------------------
  //
  // bvnr_reader_set_unit_policy / bvn_dom_parse_policy, as data: what the
  // document must contain, and what unit the consumer wants values delivered
  // in. The C setter parses every unit string and rule path when it is
  // installed, so each add below is checked by installing the whole candidate
  // policy on a throwaway reader — the library is the oracle for what it
  // accepts, and a refused add leaves the policy as it was.
  function createPolicy() {
    needExport('bvnr_wasm_policy_create', 'unit policies');
    const handle = Module.ccall('bvnr_wasm_policy_create', 'number', [], []);
    if (!handle) throw new Error('bovnar: out of memory');
    let disposed = false;
    let normalise = 0, base = 0, onInexact = 0;
    // BVNR_MAX_UNIT_TARGETS / BVNR_MAX_UNIT_RULES. Counted here so an
    // over-limit add names the limit instead of the catch-all "check the
    // unit" message the C side's single bool forces.
    let nTargets = 0, nRules = 0, nRequire = 0;

    function live() {
      if (disposed) throw new Error('bovnar: the unit policy has been disposed');
      return handle;
    }
    // bvn_rational_base_valid: 0 (the value's own) or a base bvnr can write.
    function baseValid(b) {
      return b === 0 || (b >= 2 && b <= 62) || b === 64 || b === 85;
    }
    function needBase(b, what) {
      if (!baseValid(b))
        throw new RangeError(
          `bovnar: ${what} base ${b} is not one bovnar can write ` +
          `(0 or 2..62, 64, 85)`);
    }
    function need(ok, what) {
      if (!ok) {
        throw new Error(
          `bovnar: the unit policy refused ${what}: a unit must be one ` +
          `bovnar can parse, a base one it can write (0 or 2..62, 64, 85), ` +
          `and a rule path must start with '.', be at most 96 bytes and ` +
          `cannot be just '.*'`);
      }
    }
    function withString(text, fn) {
      const p = stringPtr(text, 'a unit policy string');
      try {
        return fn(p);
      } finally {
        Module._free(p);
      }
    }

    const api = {
      /** Every numeric value must carry a unit. */
      requireUnit(on = true) {
        if (typeof on !== 'boolean')
          throw new TypeError('bovnar: requireUnit takes a boolean');
        need(Module.ccall('bvnr_wasm_policy_require_unit', 'number',
                          ['number', 'number'], [live(), on ? 1 : 0]),
             'requireUnit');
        return api;
      },
      /** Convert every numeric value to the first target it validly converts
       *  to; `base` renders the result in that base (0 keeps the value's). */
      target(unit, targetBase = 0) {
        const h = live();
        if (nTargets >= 8)
          throw new RangeError('bovnar: a unit policy takes at most 8 targets');
        const b = uint(targetBase, 0x7fffffff, 'target base');
        needBase(b, 'target');
        need(withString(unit, (p) => Module.ccall(
               'bvnr_wasm_policy_target', 'number',
               ['number', 'number', 'number'], [h, p, b])),
             `target ${JSON.stringify(String(unit))}`);
        nTargets++;
        return api;
      },
      /** Every numeric value must be validly CONVERTIBLE to `unit` (the
       *  document's own unit and value are kept). Named for the C field it
       *  fills, require_dimension_of. */
      requireConvertibleTo(unit) {
        const h = live();
        if (nRequire >= 8)
          throw new RangeError(
            'bovnar: a unit policy takes at most 8 requireConvertibleTo units');
        need(withString(unit, (p) => Module.ccall(
               'bvnr_wasm_policy_require_dimension', 'number',
               ['number', 'number'], [h, p])),
             `requireConvertibleTo ${JSON.stringify(String(unit))}`);
        nRequire++;
        return api;
      },
      /** A rule about one key path. `mode` is 'convert' (deliver converted) or
       *  'require' (assert but deliver as written). First match wins; a rule
       *  that matches nothing is satisfied. */
      rule(path, unit, opts = {}) {
        const h = live();
        // The third argument is an options object like every other one: null
        // means "not set", an unknown key is a typo and throws (a misspelled
        // `mode` used to fall back to 'convert' in silence).
        if (opts === null) opts = {};
        if (typeof opts !== 'object' || Array.isArray(opts))
          throw new TypeError('bovnar: rule: options must be an object');
        for (const k of Object.keys(opts))
          if (k !== 'base' && k !== 'mode')
            throw new TypeError(`bovnar: rule: unknown option "${k}"`);
        const { base: ruleBase = 0, mode = 'convert' } = opts;
        const modes = { convert: 0, require: 1 };
        if (!(mode in modes))
          throw new TypeError("bovnar: rule mode must be 'convert' or 'require'");
        if (nRules >= 8)
          throw new RangeError('bovnar: a unit policy takes at most 8 rules');
        const b = uint(ruleBase, 0x7fffffff, 'rule base');
        needBase(b, 'rule');
        need(withString(path, (pp) => withString(unit, (up) => Module.ccall(
               'bvnr_wasm_policy_rule', 'number',
               ['number', 'number', 'number', 'number', 'number'],
               [h, pp, up, b, modes[mode]]))),
             `rule ${JSON.stringify(String(path))}`);
        nRules++;
        return api;
      },
      /** Deliver everything the targets did not match in coherent SI base
       *  units (prefixes folded out). Values with no SI form are left alone. */
      normaliseSI(on = true) {
        if (typeof on !== 'boolean')
          throw new TypeError('bovnar: normaliseSI takes a boolean');
        setMode(on ? 1 : 0, base, onInexact);
        return api;
      },
      /** Output base for normaliseSI (0 keeps each value's own). */
      base(n) {
        live();
        const b = uint(n, 0x7fffffff, 'policy base');
        needBase(b, 'policy');
        setMode(normalise, b, onInexact);
        return api;
      },
      /** What a policy conversion that cannot be delivered exactly does:
       *  'error' (abort, the want_unit default) or 'leave' (deliver the value
       *  untouched; read `converted` to see which happened). */
      onInexact(mode) {
        live();
        const m = { error: 0, leave: 1 }[mode];
        if (m === undefined)
          throw new TypeError("bovnar: onInexact must be 'error' or 'leave'");
        setMode(normalise, base, m);
        return api;
      },
      /** Release the policy. Using it afterwards throws; idempotent. */
      dispose() {
        if (disposed) return;
        disposed = true;
        Module.ccall('bvnr_wasm_policy_destroy', 'void', ['number'], [handle]);
      },
      get disposed() { return disposed; },
      /** Internal: what policyInfo() reads. A Symbol method, so no object
       *  outside this closure can present itself as a policy — and a handle
       *  is never handed to the core from one that did not come from here. */
      [POLICY]() { return { module: Module, handle: disposed ? 0 : handle }; },
    };

    function setMode(n, b, i) {
      const ok = Module.ccall('bvnr_wasm_policy_normalise', 'number',
                              ['number', 'number', 'number', 'number'],
                              [live(), n, b, i]);
      if (!ok)
        throw new RangeError('bovnar: the unit policy refused this ' +
                             'normalise/base/onInexact combination');
      normalise = n; base = b; onInexact = i;
    }
    return api;
  }

  return {
    /** Validate a document exactly as `bovnar validate` does: the streaming
     *  pass (stops at the first error), then -- if that was clean -- the
     *  document tier, which holds the rules no single value can break: array
     *  homogeneity, one shape per array of records, no repeated key. Returns
     *  { ok, error, error_name, tier, line, column, byte, offset,
     *  recovery_count, skipped_bytes, declared_version }.
     *
     *  `tier` is null when ok, 'stream' for an error at a position, and
     *  'document' for a whole-document rule -- which HAS no position: line,
     *  column, byte and offset are 0 then, and must not be shown as 0:0. It is
     *  also null for an internal failure that has no position at all (an
     *  options or policy refusal, or an allocation failure): those report
     *  error_invalid_argument.
     *
     *  `byte` is the offending input byte's VALUE; `offset` is its 0-based
     *  index into the input. `column` counts characters, not bytes. */
    validate: (input, options) =>
      callJsonOpts('bvnr_wasm_validate', 'bvnr_wasm_validate_ex',
                   input, options, READ_OPTIONS, 'validate'),

    /** Validate in resync mode, collecting EVERY error. Returns
     *  { ok, error, error_name, line, column, byte, offset, recovery_count,
     *  skipped_bytes, declared_version,
     *  errors: [{ error, error_name, line, column, byte, offset, tier }] }.
     *  `error`/`error_name` name the first, as on every other surface.
     *
     *  When the streaming pass finds nothing, the document tier is consulted
     *  as in validate(), and its error -- there is at most one -- is the single
     *  entry, with tier 'document' and a zero position. So `ok` here and in
     *  validate() always agree. */
    errors: (input, options) =>
      callJsonOpts('bvnr_wasm_errors', 'bvnr_wasm_errors_ex',
                   input, options, READ_OPTIONS, 'errors'),

    /** Parse and project the validated DOM to plain JSON. Returns
     *  { ok, error, error_name, declared_version, value }. Unit/type metadata
     *  is dropped from `value`; use events() for that. Takes only `{ policy }`
     *  from the options -- the DOM entry points take no read flags.
     *
     *  One valid document is refused, as `bovnar convert` refuses it: a
     *  datetime written with sub-second digits, whose JSON form would be a
     *  different instant. That comes back as value_out_of_range with a
     *  `detail` string naming the key. */
    toJSON: (input, options) =>
      callJsonOpts('bvnr_wasm_to_json', 'bvnr_wasm_to_json_ex',
                   input, options, DOM_OPTIONS, 'toJSON', true),

    /** The reference (verified) event stream. Returns { ok, error, error_name,
     *  line, column, byte, offset, recovery_count, skipped_bytes,
     *  declared_version, events: [{ seq, ev, tok, text?, family?, width?,
     *  base?, unit?, unit_source?, converted?, ... }] }.
     *
     *  This is the STREAMING tier, and `ok` speaks for that tier only: it is
     *  what a reader's on_verified callback sees, and a reader does not know
     *  that two keys collide or that an array's elements disagree. A document
     *  can be ok here and fail validate() with tier 'document'. Ask validate()
     *  (or errors()) whether a document is valid; ask this what it contains.
     *
     *  Runs in resync mode, so a broken assignment is skipped rather than
     *  aborting the read and its events are simply absent. `ok` accounts for
     *  that: it is false if ANY error was reported, including a recovered one,
     *  and `error`/`error_name` name the first, with its position and the
     *  reader's recovery statistics beside it.
     *
     *  A unit crosses twice: `unit` is the canonical form ("m~L") and
     *  `unit_source` the spelling the document used ("ucum:mL{total}"). The
     *  reader sets `unit_source` on every event whose value HAS a unit, even
     *  when the spelling is already canonical ("3 m" carries unit_source "m");
     *  it is absent only where there is no unit. (`doc()`'s `unitSource` is
     *  the DOM's and follows its own rule: null when the spelling is
     *  canonical.) `base` 0 on a value means "no explicit base", i.e. decimal
     *  10; on a datetime it is the EPOCH INDEX, whose name is in `epoch`.
     *
     *  An octet chunk's data event reports `bytes`; pass `{ octets: 'hex' }`
     *  to also carry the payload as `hex`, the only way to read the binary
     *  part of a document through this module (JSON text cannot hold it
     *  directly). Off by default: hex doubles the payload. */
    events: (input, options) =>
      callJsonOpts('bvnr_wasm_events', 'bvnr_wasm_events_ex',
                   input, options, EVENT_OPTIONS, 'events'),

    /** Events AND errors from a SINGLE reader pass:
     *  { ok, error, error_name, line, column, byte, offset, recovery_count,
     *  skipped_bytes, declared_version, events:[...], errors:[...] }.
     *  Prefer this over calling events() and errors() for the same document —
     *  that walks it twice and pays two JSON round-trips.
     *
     *  Like events(), this is the STREAMING tier: `errors` holds what the
     *  reader raised, each with tier 'stream', and never a document-tier
     *  error -- so it can be ok for a document validate() refuses. (A caller
     *  that needs both pays for the second pass by calling validate() or
     *  doc() when this one comes back clean.)
     *
     *  `ok` is false if ANY error was reported, including ones the reader
     *  recovered from, and `error`/`error_name` name the first — matching
     *  errors()[0] and events(). All three count reported errors rather than
     *  reading the reader's final code, which continue_on_error clears on a
     *  clean EOF; deriving the status from that code alone reported
     *  {"ok":false,"error_name":"none"} here, and plain {"ok":true} in events().
     *
     *  Takes the same options as events(), including `octets: 'hex'`.
     *
     *  Defined only when the compiled module actually exports it: this wrapper
     *  and the core are separate URLs with separate cache entries, so a current
     *  wrapper can be paired with a stale core. Callers feature-test `parse`,
     *  and an unconditional definition would make that test lie and turn a
     *  recoverable fallback into a throw. */
    ...(typeof Module._bvnr_wasm_parse === 'function'
        ? { parse: (input, options) =>
              callJsonOpts('bvnr_wasm_parse', 'bvnr_wasm_parse_ex',
                           input, options, EVENT_OPTIONS, 'parse') }
        : {}),

    /** The event stream with the reader's LOSSLESS unit/base conversion armed,
     *  so numeric events also carry `converted` (the exact value, or null when
     *  it has no finite expansion in `base`), `converted_base` and
     *  `converted_unit`. When `converted` is null the exact result has no
     *  finite expansion in `base`, and the exact rational is carried as
     *  `converted_num`/`converted_den` decimal strings instead (its exact value
     *  in that case; they are always the exact reduced rational, den > 0).
     *
     *  `unit` is a unit string ("m", "k~m/s"); pass '' to keep each value's own
     *  unit, which together with a `base` is a pure base conversion. `base` is
     *  the output base (0 keeps the value's own; otherwise 2..62, 64 or 85).
     *  With `allowNonterminating` a result that is exact as a rational but does
     *  not terminate in `base` arrives as `"converted": null` instead of being
     *  skipped. An unparseable `unit` comes back as error_unit_illegal and an
     *  unusable `base` as error_invalid_argument, before any parsing.
     *
     *  Note: this surface runs in resync mode, so a value the conversion
     *  rejects is skipped along with its assignment rather than aborting the
     *  call — it will not appear in `events` — but `ok` is then false and
     *  `error_name` names the rejection (unit_mismatch, unit_inexact).
     *
     *  Takes the same options as events(), including `octets: 'hex'`. */
    eventsConvert: (input, unit = '', base = 0, allowNonterminating = false,
                    options) =>
      callJsonConvert(input, unit, base, allowNonterminating, options),

    /** Full document tree in document order, each value carrying its resolved
     *  type fields (familyName/width/base/unit/unitSource/epoch), a datetime's
     *  sub-second digits as `frac`, and struct/array/octet shapes. Returns
     *  { ok, error, error_name, declared_version, tree: [{ key, value }] }.
     *  Built from the DOM, so `ok` covers both tiers (the error carries no
     *  tier field: the DOM reports the code alone). Used by the web
     *  playground's parser shim to reconstruct the tree + event-stream shapes.
     *  Takes only `{ policy }`. */
    doc: (input, options) =>
      callJsonOpts('bvnr_wasm_doc', 'bvnr_wasm_doc_ex',
                   input, options, DOM_OPTIONS, 'doc', true),

    /** Library version string, e.g. "2.0.2" (BVNR_VERSION_STRING). */
    version: () => Module.ccall('bvnr_wasm_version', 'string', [], []),

    /** The same version as the monotonic integer BVNR_VERSION, for comparison
     *  (#if-style: 2.0.2 is 20002). */
    versionNumber: () => {
      needExport('bvnr_wasm_version_number', 'versionNumber');
      return Module.ccall('bvnr_wasm_version_number', 'number', [], []);
    },

    /** Highest bovnar SPEC version this build understands, e.g.
     *  { major: 1, minor: 2 }. Not the library version: a document declares
     *  its own with a "#!bovnar" directive. */
    specVersion: () => {
      needExport('bvnr_wasm_spec_version', 'specVersion');
      const p = Module.ccall('bvnr_wasm_spec_version', 'number', [], []);
      return { major: (p >>> 16) & 0xffff, minor: p & 0xffff };
    },

    /** The spec version a document declares in its leading "#!bovnar <maj>.<min>"
     *  directive, without parsing it: { major, minor }, or null when there is
     *  no well-formed directive. */
    peekVersion: (input) => {
      needExport('bvnr_wasm_peek_version', 'peekVersion');
      const bytes = toBytes(input);
      const inPtr = malloc(bytes.length || 1);
      let foundPtr = 0;
      // An out-parameter rather than a sentinel: 65535.65535 packs to the same
      // 32 bits a sentinel would have to use.
      try {
        foundPtr = malloc(4);
        if (bytes.length) Module.HEAPU8.set(bytes, inPtr);
        const p = Module.ccall('bvnr_wasm_peek_version', 'number',
                               ['number', 'number', 'number'],
                               [inPtr, bytes.length, foundPtr]);
        if (!new DataView(Module.HEAPU8.buffer, foundPtr, 4)
              .getUint32(0, true)) return null;
        return { major: (p >>> 16) & 0xffff, minor: p & 0xffff };
      } finally {
        Module._free(inPtr);
        if (foundPtr) Module._free(foundPtr);
      }
    },

    /** Name for a numeric error code, as error_name spells it in every result
     *  ("none" for 0; "unknown_error" for a code this build does not define,
     *  which is bvn_error_to_string's own answer). */
    errorName: (code) => {
      needExport('bvnr_wasm_error_name', 'errorName');
      return Module.ccall('bvnr_wasm_error_name', 'string',
                          ['number'], [code | 0]);
    },

    /** Parse a unit string: { ok, unit, error, error_name, profile_only }.
     *  `unit` is the canonical spelling, or null for a unit that parses but has
     *  no text (an opaque profile unit beside another component, say -- see
     *  bvn_unit_to_string in the C API). `profile_only` says the unit has no
     *  native spelling. A refusal names WHY: unit_illegal, profile_unknown or
     *  profile_unsupported. */
    parseUnit: (text) => {
      needExport('bvnr_wasm_unit', 'parseUnit');
      return callJsonCString('bvnr_wasm_unit', text, 'a unit');
    },

    /** Parse the body of a type annotation — "uint:32,_16,k~g" — as the reader
     *  parses one, without a document: bvn_parse_type_annotation. The angle
     *  brackets are optional here (a "<...>" wrapper is stripped); the body
     *  alone is what crosses.
     *
     *  Returns { ok, type_ok, unit_ok, unit_too_long, family, width, base,
     *  epoch, unit, unit_source, profile_only }. `ok` is type_ok && unit_ok &&
     *  !unit_too_long. The flags keep the C API's failure kinds apart: an
     *  annotation that does not parse (an unknown family, or a base that
     *  family cannot carry — <float:64,_8>, <uint:64,_0>), a unit parameter
     *  that is not a unit, and a unit parameter over 255 bytes.
     *
     *  This is the reader's own acceptance test for the ANNOTATION; it does
     *  not see a value, so a well-formed <uint:8> against a string still
     *  passes here. `width` and `base` are the parsed spec's fields, not the
     *  reader's resolved view: 0 means "not written" (the reader resolves
     *  those to 64 and 10), a float_fix keeps its q in `base` and a datetime
     *  its epoch index there, with the epoch's name beside it in `epoch`.
     *  `family`/`width`/`base` are null when type_ok is false; `unit` is the
     *  canonical spelling (null for a unit with no text), and `unit_source`
     *  the parameter as written — including "no_unit", so an explicit no_unit
     *  is distinguishable from an omitted one (null). A bad annotation is
     *  data, as with parseUnit: it never throws.
     *
     *  There is no document here, so there is no declared spec version: a
     *  profile spelling ("ucum:mm[Hg]") parses as a unit, while a DOCUMENT
     *  carrying it still needs "#!bovnar 1.2" and reports unit_illegal
     *  without one. Use validate() when the document's rules are what you
     *  need. */
    parseTypeAnnotation: (text) => {
      needExport('bvnr_wasm_parse_type_annotation', 'type annotations');
      let s = String(text);
      if (s.length >= 2 && s[0] === '<' && s[s.length - 1] === '>')
        s = s.slice(1, -1);
      return callJsonCString('bvnr_wasm_parse_type_annotation', s,
                             'a type annotation');
    },

    /** A unit written as a code in a profile's vocabulary: bvn_unit_to_profile,
     *  with 'ucum' the default (bvn_unit_to_ucum). Returns
     *  { ok, error, error_name, unit }, where `unit` is the code WITHOUT its
     *  "ns:" prefix, or null for a VALID unit that vocabulary has no code for.
     *  Both directions are partial: a currency has no UCUM code, a compound has
     *  no flat-vocabulary code, and an unknown namespace is null for every
     *  unit. An unparseable input is a refusal, as in parseUnit(). */
    unitToProfile: (text, ns = 'ucum') => {
      needExport('bvnr_wasm_unit_to_profile', 'unitToProfile');
      // null means "the default namespace" as undefined does; otherwise
      // String(null) would become a namespace literally called "null".
      return callJsonCString2('bvnr_wasm_unit_to_profile', text,
                              ns == null ? 'ucum' : ns);
    },

    /** Whether two unit strings denote the same unit. Throws TypeError when
     *  either is not a unit at all (parseUnit() reports that as data). */
    unitsEqual: (a, b) => {
      needExport('bvnr_wasm_units_equal', 'unit comparison');
      const pa = stringPtr(a, 'a unit');
      let pb = 0;
      try {
        pb = stringPtr(b, 'a unit');
        const r = Module.ccall('bvnr_wasm_units_equal', 'number',
                               ['number', 'number'], [pa, pb]);
        if (r < 0)
          throw new TypeError(
            'bovnar: unitsEqual: one of the arguments is not a unit');
        return r === 1;
      } finally {
        Module._free(pa);
        if (pb) Module._free(pb);
      }
    },

    /** Whether `a` could be converted to `b` at all -- bvn_units_convertible,
     *  the predicate the C API says to SCREEN a conversion target with. It is
     *  a screen, not a guarantee: `s/°C` passes it and the conversion still
     *  declines. Throws TypeError when either argument is not a unit. */
    unitsConvertible: (a, b) => {
      needExport('bvnr_wasm_units_convertible', 'unit compatibility');
      const pa = stringPtr(a, 'a unit');
      let pb = 0;
      try {
        pb = stringPtr(b, 'a unit');
        const r = Module.ccall('bvnr_wasm_units_convertible', 'number',
                               ['number', 'number'], [pa, pb]);
        if (r < 0)
          throw new TypeError(
            'bovnar: unitsConvertible: one of the arguments is not a unit');
        return r === 1;
      } finally {
        Module._free(pa);
        if (pb) Module._free(pb);
      }
    },

    /** Convert one number from one unit to another: bvn_unit_convert_value,
     *  the C API's double path (affine scales such as °C/°F included). Returns
     *  { ok, value } or { ok:false, error, error_name, value:null }.
     *
     *  This rounds where a double rounds. The EXACT arbitrary-precision path is
     *  the reader's want_unit hook -- eventsConvert() -- and that is where a
     *  consumer that must not round should go. */
    convertUnit: (value, from, to) => {
      needExport('bvnr_wasm_convert_unit', 'convertUnit');
      const pa = stringPtr(from, 'a unit');
      let pb = 0, outPtr = 0;
      try {
        pb = stringPtr(to, 'a unit');
        outPtr = Module.ccall('bvnr_wasm_convert_unit', 'number',
          ['number', 'number', 'number'], [Number(value), pa, pb]);
        return readJson(outPtr);
      } finally {
        Module._free(pa);
        if (pb) Module._free(pb);
        if (outPtr)
          Module.ccall('bvnr_wasm_free', 'void', ['number'], [outPtr]);
      }
    },

    /** The compiled-in unit profiles: { ok, profiles: ["ucum", ...] }. */
    unitProfiles: () => {
      needExport('bvnr_wasm_unit_profiles', 'unit profile listing');
      return callJsonNoArg('bvnr_wasm_unit_profiles');
    },

    /** Create a unit policy (bvnr_reader_set_unit_policy / bvn_dom_parse_policy
     *  as data). Chain the methods, pass the result as `options.policy` on any
     *  surface, and dispose() it when done. See createPolicy above. */
    createPolicy,

    /** The raw emscripten Module, for advanced use. */
    Module,
  };
}

export default loadBovnar;
