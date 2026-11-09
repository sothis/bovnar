// Contract test for web/bovnar_parser_wasm.js — the playground's parser shim.
//
// Everything else the playground shows IS the C core: bvnr_wasm_parse delivers
// the reference verified event stream, and the page renders it. The shim is the
// one piece in between that reasons on its own — it walks that flat event stream
// and rebuilds the document tree from it, because the stream is what the page
// promises to display ("the same events, in the same order, that bvnr_read()
// delivers").
//
// That walk is unobservable from the C side, so nothing caught it when it was
// wrong: a struct inside an array row was treated as a keyed assignment in the
// enclosing scope, and `.a = [{.b=3;},{.c=4;}];` rendered as two top-level
// structs — one of them keyed `null` — wrapped around a phantom 1×0 array. The
// array itself never appeared. This test exists so that class of bug cannot come
// back silently.
//
// Three gates, each against a reference the shim does not get to define:
//
//   tree    parseFaithful()'s tree must equal bvnr_wasm_doc()'s DOM projection —
//           the C core's own materialised view of the same document — node for
//           node, for every document the DOM accepts.
//   events  parseBovnar()'s translated stream must be well nested and balanced.
//           ev_assignment_end is the shim's own invention (bvnr_event_t has no
//           such member), so nothing in C can check it: every ASSIGNMENT_START
//           must be closed exactly once, and never by an array element.
//   errors  whatever `bovnar validate` rejects must reach parseFaithful().errors.
//           The streaming reader does not see the document-tier rules (array
//           homogeneity, struct shape, duplicate keys), so the playground used to
//           report a green "ok · no errors" for documents the CLI refuses.
//
// Run:  node wasm/test/playground_shim_test.mjs      (from the repo root)
// Requires: a built dist/wasm/ and a built ./build/bovnar CLI (BOVNAR_CLI overrides).

import { loadBovnar } from '../../dist/wasm/index.mjs';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { readFileSync, readdirSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const CLI = process.env.BOVNAR_CLI || join(ROOT, 'build', 'bovnar');
// BOVNAR_SHIM points the test at another copy of the shim -- for trying a change
// to it against a rebuilt core before it lands in web/.
const SHIM = process.env.BOVNAR_SHIM || join(ROOT, 'web', 'bovnar_parser_wasm.js');
const tmp = mkdtempSync(join(tmpdir(), 'bvnr-shim-'));
process.on('exit', () => { try { rmSync(tmp, { recursive: true, force: true }); } catch {} });

let pass = 0, fail = 0;
const fails = [];
function ok(cond, msg) { if (cond) pass++; else { fail++; fails.push(msg); } }

// --- load the shim ---------------------------------------------------------
/*
 * The shim is a browser script: an IIFE that assigns window.BovnarParser and
 * pulls the WASM wrapper in with a cache-stamped dynamic import. Give it a
 * `window` to write to, and swap that one import for the module instance this
 * test already loaded — so both sides of every comparison below run on the SAME
 * core, and a mismatch can only come from the shim's own logic.
 */
const SHIM_IMPORT = /import\((['"])\.\/bovnar_wasm\.js[^'"]*\1\)/;
function loadShim(bvnr) {
  const src = readFileSync(SHIM, 'utf8');
  if (!SHIM_IMPORT.test(src))
    throw new Error(`${SHIM} no longer dynamic-imports ./bovnar_wasm.js — `
      + 'this test patches that import and must be updated with it');
  const patched = src.replace(SHIM_IMPORT, '__loadWasmModule()');
  const window = { dispatchEvent() {}, console };
  new Function('window', '__loadWasmModule', patched)(
    window, () => Promise.resolve({ loadBovnar: () => Promise.resolve(bvnr) }));
  return window.BovnarParser;
}

// --- tree comparison -------------------------------------------------------
const show = (x) => JSON.stringify(x);

/*
 * Compare shape — who is a container, who is an element, how many rows, which
 * key each field carries — plus the value facts both sides agree on: the text,
 * the unit and the type family.
 *
 * The two views spell a value differently, and neither spelling is the shim's
 * decision: the event stream carries the SOURCE text — which is what the
 * playground shows, and must — while the DOM carries the canonical decimal value
 * (`007` → `7`, `0.50` → `0.5`, `<uint:4,_16> "F"` → `15`, `ninf` → `-inf`, a
 * reference keeping its `&` sigil). So text is compared as a value, in the base
 * the annotation declares. Also out: `base` (the stream reports the synthesised
 * default as 0, the DOM as 10), `width` (the DOM omits it for a datetime) and
 * the null literal's family (`plain` in the DOM, absent from the stream).
 */
function sameText(a, b, base) {
  if (a === b) return true;
  if (a === 'ninf' && b === '-inf') return true;      // canonical spelling of -∞
  const na = Number(a), nb = Number(b);
  if (a !== '' && b !== '' && Number.isFinite(na) && Number.isFinite(nb) && na === nb)
    return true;
  if (base && base !== 10) {                          // "F" base 16 == 15
    const ia = base <= 36 ? parseInt(a, base) : NaN;
    if (Number.isFinite(ia) && Number.isFinite(nb) && ia === nb) return true;
    /* Beyond parseInt's reach: base 62, and a base-16 float ("1.921fb54442d18p+1")
       whose binary exponent Number() cannot read either. The decimal on the DOM
       side is the C core's own conversion and the shim only forwards the source
       text, so checking that the text is a legal digit string in the declared
       base is as far as a value comparison can go here. Above base 62 the
       alphabet takes in punctuation ("#" is a base-85 digit) and this test does
       not model it; the structural, unit and family checks still apply. */
    if (base > 62 || validDigits(a, base)) return true;
  }
  return false;
}
function digitValue(ch, base) {
  const c = ch.charCodeAt(0);
  if (c >= 48 && c <= 57) return c - 48;                              // 0-9
  if (base > 36) {                                                    // case-significant
    if (c >= 65 && c <= 90) return c - 55;                            // A-Z
    if (c >= 97 && c <= 122) return c - 61;                           // a-z
    return -1;
  }
  const l = ch.toLowerCase().charCodeAt(0);
  return l >= 97 && l <= 122 ? l - 87 : -1;
}
function validDigits(text, base) {
  let s = String(text).replace(/^[-+]/, '');
  if (base === 16) s = s.split(/[pP]/)[0];            // strip the binary exponent
  s = s.replace('.', '');
  return s.length > 0 && [...s].every(ch => {
    const v = digitValue(ch, base);
    return v >= 0 && v < base;
  });
}
const family = (v) => (v.familyName && v.familyName !== 'plain' ? v.familyName : null);
/* the DOM keeps a reference's '&' sigil; the event's text is the target alone */
const refText = (v) => (v.type === 'reference' ? String(v.text).replace(/^&/, '') : v.text);

/* Returns the first difference as a path + description, or null when equal. */
function diffValue(a, b, path) {
  if (!a || !b) return a === b ? null : `${path}: present on only one side`;
  if (a.type !== b.type) return `${path}: ${a.type} vs ${b.type}`;
  if (a.type === 'array') {
    if (a.dims !== b.dims) return `${path}: dims ${a.dims} vs ${b.dims}`;
    if (a.rows.length !== b.rows.length)
      return `${path}: ${a.rows.length} row(s) vs ${b.rows.length}`;
    for (let r = 0; r < a.rows.length; r++) {
      const ea = a.rows[r].elements, eb = b.rows[r].elements;
      if (ea.length !== eb.length)
        return `${path}[row ${r}]: ${ea.length} element(s) vs ${eb.length}`;
      for (let i = 0; i < ea.length; i++) {
        const d = diffValue(ea[i], eb[i], `${path}[${r}][${i}]`);
        if (d) return d;
      }
    }
    return null;
  }
  if (a.type === 'struct') {
    if (a.children.length !== b.children.length)
      return `${path}: ${a.children.length} field(s) vs ${b.children.length}`;
    for (let i = 0; i < a.children.length; i++) {
      const ca = a.children[i], cb = b.children[i];
      if (ca.key !== cb.key)
        return `${path}: field ${i} keyed ${show(ca.key)} vs ${show(cb.key)}`;
      const d = diffValue(ca.value, cb.value, `${path}.${ca.key}`);
      if (d) return d;
    }
    return null;
  }
  /* Byte count only: chunking is a wire detail the stream exposes one ev_data at
     a time and the DOM joins into a single payload. */
  if (a.type === 'octet')
    return a.total === b.total ? null : `${path}: ${a.total} B vs ${b.total} B`;
  if (!sameText(refText(a), refText(b), b.base))
    return `${path}: text ${show(a.text)} vs ${show(b.text)}`;
  if ((a.unit || null) !== (b.unit || null))
    return `${path}: unit ${show(a.unit || null)} vs ${show(b.unit || null)}`;
  if (family(a) !== family(b))
    return `${path}: family ${show(family(a))} vs ${show(family(b))}`;
  return null;
}

/* shim tree.children and DOM tree entries are both [{ key, value }]. */
function diffTree(shimTree, domEntries) {
  const a = shimTree.children || [], b = domEntries || [];
  if (a.length !== b.length)
    return `${a.length} top-level assignment(s) vs ${b.length}`;
  for (let i = 0; i < a.length; i++) {
    if (a[i].key !== b[i].key)
      return `assignment ${i} keyed ${show(a[i].key)} vs ${show(b[i].key)}`;
    const d = diffValue(a[i].value, b[i].value, `.${a[i].key}`);
    if (d) return d;
  }
  return null;
}

// --- event-stream well-formedness -----------------------------------------
/*
 * One stack for all four bracketing pairs: a stream is well formed only if every
 * end matches the innermost start. That is exactly what broke — an element
 * struct emitted an ev_assignment_end that closed an assignment it did not open,
 * and the assignment that owned the array was never closed at all.
 */
function checkStream(P, text) {
  const EV = P.EV;
  const stack = [], bad = [];
  let starts = 0, ends = 0;
  P.parseBovnar(text, (ev) => {
    switch (ev.type) {
      case EV.ASSIGNMENT_START:   starts++; stack.push('assignment'); break;
      case EV.ASSIGNMENT_END:     ends++;   expect('assignment');     break;
      case EV.STRUCT_START:       stack.push('struct');               break;
      case EV.STRUCT_END:         expect('struct');                   break;
      case EV.ARRAY_ROW_START:    stack.push('row');                  break;
      case EV.ARRAY_ROW_END:      expect('row');                      break;
      case EV.OCTET_STREAM_START: stack.push('octet');                break;
      case EV.OCTET_STREAM_END:   expect('octet');                    break;
    }
  });
  function expect(kind) {
    const got = stack.pop();
    if (got !== kind) bad.push(`${kind} end closed ${got || 'nothing'}`);
  }
  if (stack.length) bad.push(`left open: ${stack.join(', ')}`);
  if (starts !== ends) bad.push(`${starts} assignment_start vs ${ends} assignment_end`);
  return bad;
}

/*
 * The translated stream may CLOSE a container the C left open (drain does that
 * so a consumer's nesting returns to the top), but it must not drop or invent
 * any other event -- stream_end is never synthesised, and every start, data,
 * type and annotation-parameter event the reader delivered must arrive. This
 * caught `no_unit` being filtered out of the parameter events: an explicit
 * `<uint:8,no_unit>` and a plain `<uint:8>` were indistinguishable, and a
 * synthesised annotation lost a parameter the reader had emitted.
 */
const C_EV_TO_SHIM = {
  stream_start: 'STREAM_START', stream_end: 'STREAM_END',
  assign_start: 'ASSIGNMENT_START',
  struct_open: 'STRUCT_START', struct_close: 'STRUCT_END',
  array_row_start: 'ARRAY_ROW_START', array_row_end: 'ARRAY_ROW_END',
  array_dim_start: 'ARRAY_DIM_START',
  octet_start: 'OCTET_STREAM_START', octet_end: 'OCTET_STREAM_END',
  type_start: 'TYPE_ANN_START', type_family: 'TYPE_ANN_FAMILY',
  type_param: 'TYPE_ANN_PARAM', type_end: 'TYPE_ANN_END',
  data: 'DATA',
};
function eventCountDiff(P, cEvents, text) {
  const want = {}, got = {};
  for (const e of cEvents) want[e.ev] = (want[e.ev] || 0) + 1;
  P.parseBovnar(text, (ev) => {
    for (const [ce, se] of Object.entries(C_EV_TO_SHIM))
      if (ev.type === P.EV[se]) got[ce] = (got[ce] || 0) + 1;
  });
  const bad = [];
  for (const ce of Object.keys(C_EV_TO_SHIM)) {
    const w = want[ce] || 0, g = got[ce] || 0;
    const closer = ce === 'struct_close' || ce === 'array_row_end' || ce === 'octet_end';
    if (closer ? g < w : g !== w) bad.push(`${ce}: ${g} vs ${w}`);
  }
  return bad;
}

// --- corpus ----------------------------------------------------------------
/*
 * The shim's input is what a browser textarea holds: a JS string, UTF-8 encoded
 * on the way in. An example carrying arbitrary binary (an octet stream with
 * high bytes) cannot survive that round trip and is not reachable from the
 * playground at all, so it is skipped rather than compared against a document
 * the decode already corrupted. Skips are named, not silent — the ASCII octet
 * cases in `shapes` below keep that path covered.
 */
const exDir = join(ROOT, 'examples');
const skipped = [];
const examples = readdirSync(exDir).filter(f => f.endsWith('.bvnr')).map((f) => {
  const raw = readFileSync(join(exDir, f));
  const text = raw.toString('utf8');
  if (!Buffer.from(text, 'utf8').equals(raw)) { skipped.push(f); return null; }
  return { name: `examples/${f}`, text };
}).filter(Boolean);

/* Shapes the flat "one array is being built" walk could not express. Every one
   of these was rendered wrong before the container stack landed. */
const shapes = [
  { name: 'array of structs',            text: '.a = [{.b=3;},{.b=4;}];\n' },
  { name: 'array of structs, nested',    text: '.s = {.rows = [{.b=1;},{.b=2;}];};\n' },
  { name: 'struct elements with arrays', text: '.a = [{.x=1;.y=[1,2];},{.x=2;.y=[3,4];}];\n.tail = 7;\n' },
  { name: 'nested array',                text: '.cube = [[1,2],[3,4]];\n' },
  { name: 'nested array, three deep',    text: '.d = [[[1,2],[3,4]],[[5,6],[7,8]]];\n' },
  { name: 'dimension rows',              text: '.m = [1,2]/[3,4];\n' },
  { name: 'dimension rows, 3x3',         text: '.m = [1,2,3]/[4,5,6]/[7,8,9];\n' },
  { name: 'dimension rows inside array', text: '.q = [[1,2]/[3,4],[5,6]/[7,8]];\n' },
  { name: 'array as last struct field',  text: '.s = {.a = 1; .arr = [1,2];};\n.t = 9;\n' },
  { name: 'array then struct',           text: '.a = [1,2];\n.b = {.c = 1;};\n' },
  { name: 'deep struct nesting',         text: '.d = {.a = {.b = {.c = [1,2]/[3,4];};};};\n.after = 1;\n' },
  { name: 'empty array and struct',      text: '.e = [];\n.s = {};\n' },
  { name: 'null and string elements',    text: '.n = [null,1];\n.s = ["x","y"];\n' },
  { name: 'octet stream',                text: '.bin = \x00\x01\x05\x00hello\x01\x03\x00bye\x00;\n.after = 1;\n' },
  { name: 'octet stream in a struct',    text: '.s = {.bin = \x00\x01\x02\x00hi\x00; .t = 2;};\n' },
  { name: 'typed and united scalars',    text: '.v = <float:64,m/s> 9.81;\n.b = true;\n.s = "hi";\n' },
];

/* Rejected above the streaming reader: bvnr_read() reports none of these, so
   they are the cases the playground used to call green. The expected code comes
   from the CLI, not from a literal here. */
const docTier = [
  { name: 'struct shape mismatch', at: [1, 1],  text: '.a = [{.b=3;},{.c=4;}];\n' },
  { name: 'duplicate key',         at: [2, 1],  text: '.x = 1;\n.x = 2;\n' },
  { name: 'heterogeneous array',   at: [1, 1],  text: '.mix = [1,"two"];\n' },
  { name: 'ragged nested array',   at: [1, 1],  text: '.r = [[1,2],[3,4,5]];\n' },
  /* the site is a node, not the first line: an indented repeat inside a struct,
     and a repeat inside an array element */
  { name: 'duplicate key in a struct', at: [4, 5],
    text: '.s = {\n    .x = 1;\n    .y = 2;\n    .x = 9;\n};\n' },
  { name: 'duplicate key in an element', at: [1, 14],
    text: '.rs = [{.b=1;.b=2;},{.b=3;.b=4;}];\n' },
];

/*
 * Rejected by the streaming reader, mid-document: resync must keep the tree and
 * the event stream coherent for everything that follows, and `after` names the
 * assignments that must still be assignments at the top level.
 *
 * The reader does not unwind a broken element the way it built it -- for a bad
 * field inside an array's element struct it ends the ROW first and only then
 * closes the struct. Read literally that leaves the row open for the rest of the
 * document and every later assignment lands in it as an ELEMENT, key and all
 * dropped: `.c` and `.d` below were gone from the tree entirely, and the array
 * reported three elements. A truncated document is the other end of the same
 * problem -- it gets no ev_stream_end at all, so nothing closes what is open.
 */
const resync = [
  /* the offending assignment is skipped, not repaired -- resync shows every
     well-formed assignment PAST an error, which is why `small` is absent here */
  { name: 'range error then more',    after: ['ok', 'after'],
    text: '.small = <uint:8> 999;\n.ok = [1,2];\n.after = {.z = 1;};\n' },
  { name: 'bad field in a struct',    after: ['s', 't'],
    text: '.s = {.bad = <uint:8> 999;};\n.t = 1;\n' },
  { name: 'bad array element',        after: ['a', 'b'],
    text: '.a = [1, <uint:8> 999, 3];\n.b = 2;\n' },
  { name: 'bad field in an element',  after: ['a', 'c', 'd'],
    text: '.a = [{.b = <uint:8> 999;}, {.b=1;}];\n.c = 42;\n.d = 7;\n' },
  /* The struct_close that follows such a row end has already been spent closing
     the element. Spending it twice pops the ENCLOSING struct, and `.z` -- a
     field of `.s` -- surfaces at the top level. */
  { name: 'bad element inside a struct', after: ['s', 't'],
    text: '.s = {.a = [{.b = <uint:8> 999;}, {.b=1;}]; .z = 5;};\n.t = 1;\n' },
  { name: 'bad element, struct ends after', after: ['s', 't'],
    text: '.s = {.a = [{.b = <uint:8> 999;}];};\n.t = 1;\n' },
  { name: 'unterminated array',       after: ['a', 'b'],
    text: '.a = [1, 2;\n.b = 2;\n' },
  { name: 'unterminated array in a struct', after: ['s', 't'],
    text: '.s = {.a = [1,2;};\n.t = 1;\n' },
  { name: 'ragged dimension rows',    after: ['a', 'b'],
    text: '.a = [1,2]/[3];\n.b = 1;\n' },
  { name: 'truncated document',       after: ['a'],
    text: '.a = {.x = 1;\n.b = 2;\n' },
];

function cliValidate(text) {
  const f = join(tmp, 'in.bvnr');
  writeFileSync(f, text);
  try {
    execFileSync(CLI, ['validate', f], { encoding: 'utf8' });
    return { ok: true, error_name: 'none' };
  } catch (e) {
    const out = (e.stdout || '') + (e.stderr || '');
    const m = out.match(/Validation failed:\s*(\w+)/);
    return { ok: false, error_name: m ? m[1] : '?' };
  }
}

// --- run -------------------------------------------------------------------
const bvnr = await loadBovnar();
const P = loadShim(bvnr);
for (let i = 0; !P.isWasmReady() && i < 500; i++) await new Promise(r => setTimeout(r, 10));
ok(P.isWasmReady(), 'shim never became ready');
if (!P.isWasmReady()) { console.log('shim did not load the module'); process.exit(1); }

console.log(`playground shim vs bovnar-wasm ${bvnr.version()}  |  CLI @ ${CLI}`);
console.log(`corpus: ${examples.length} example(s) + ${shapes.length} shape(s)`
  + (skipped.length ? `  |  skipped as not-UTF-8: ${skipped.join(', ')}` : '') + '\n');

console.log('# tree == the DOM projection of the same document');
for (const { name, text } of [...examples, ...shapes]) {
  const dom = bvnr.doc(text);
  if (!dom.ok) continue;               // rejected documents have no DOM to match
  const d = diffTree(P.parseFaithful(text).tree, dom.tree);
  ok(d === null, `tree ${name}: ${d}`);
}

/* The corpus above is deliberate: every shape someone thought of. This adds
   breadth — generated documents over the same constructs (typed scalars,
   units, per-element annotations, empty and multi-row arrays, records, octets)
   — and compares the shim's tree to the DOM projection for each one the DOM
   accepts. The generator biases toward validity; the count assertion fails if
   it drifts into generating nothing the DOM takes. */
console.log('# the tree matches the DOM over generated documents');
{
  let seed = 1234;
  const rnd = () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff;
  const ri = (n) => Math.floor(rnd() * n);
  const pickArr = (a) => a[ri(a.length)];
  const scalars = [
    '1', '-42', '-273.15', '9.81', '1e3', 'nan', 'inf', '-inf',
    'true', 'false', 'null', 'sym', '&.ref', '"x"', '"a b"', '""',
    '<uint:8> 7', '<sint:16> -2', '<float:64,m/s> 9.8',
    '<float_fix:16,q8,°C> 21.5', '<float_dec:64,$USD> 12.34',
    '<uint:16,_16> FF', '1 k~m', '100 deg', '\x00\x01\x02\x00ab\x00',
  ];
  const groups = [
    ['1', '2', '3'],
    ['<uint:8> 1', '<uint:8> 2'],
    ['<float:64,m/s> 1.0', '<float:64,m/s> 2.0'],
    ['1 m', '2 m', '3 m'],
    ['true', 'false', 'true'],
    ['"a"', '"b"'],
    ['1', '<uint:8> 2'],               /* mixed encodings, one physical type */
  ];
  function gen(d) {
    if (d > 2) return pickArr(scalars);
    const k = ri(10);
    if (k < 6) return pickArr(scalars);
    if (k === 6) {
      const g = pickArr(groups);
      return '[' + Array.from({ length: ri(4) }, () => pickArr(g)).join(', ') + ']';
    }
    if (k === 7) {
      const g = pickArr(groups);
      return '[' + pickArr(g) + ', ' + pickArr(g) + '] / [' +
             pickArr(g) + ', ' + pickArr(g) + ']';
    }
    if (k === 8)
      return '{' + Array.from({ length: 1 + ri(3) },
        (_, i) => '.f' + i + ' = ' + gen(d + 1) + ';').join(' ') + '}';
    const shape = Array.from({ length: 1 + ri(3) }, (_, i) => '.g' + i);
    return '[' + Array.from({ length: 1 + ri(3) }, () =>
      '{' + shape.map((key) => key + ' = ' + pickArr(scalars) + ';').join(' ') + '}'
    ).join(', ') + ']';
  }
  let valid = 0;
  for (let n = 0; n < 300; n++) {
    const text = Array.from({ length: 1 + ri(3) },
      (_, i) => '.k' + i + ' = ' + gen(0) + ';').join('\n') + '\n';
    const dom = bvnr.doc(text);
    if (!dom.ok) continue;             /* not a document the DOM accepts */
    valid++;
    const d = diffTree(P.parseFaithful(text).tree, dom.tree);
    ok(d === null, `generated ${n}: ${d} in ${JSON.stringify(text)}`);
  }
  ok(valid >= 50, `generated corpus: ${valid} valid document(s), expected 50+`);
}

console.log('# every assignment is closed exactly once, by its own value');
for (const { name, text } of [...examples, ...shapes, ...resync]) {
  const bad = checkStream(P, text);
  ok(bad.length === 0, `events ${name}: ${bad.join('; ')}`);
}

/* A read that dies inside an octet stream delivers ev_octet_stream_start and
   no end: a chunk cut short (read_complete_chunk_failed) or a framing byte out
   of sync. drain() closed structs, rows and assignments but not this pair, so
   the translated stream left an octet open and a consumer's nesting never
   returned to the top. It is closed like the others now. */
console.log('# a truncated octet stream still opens and closes once');
{
  const cases = [
    { name: 'chunk cut short', text: '.bin = \x00\x01\x05\x00ab;\n.after = 1;\n' },
    { name: 'framing out of sync', text: '.bin = \x00\x01\x05\x00ab' },
    { name: 'inside an array row', text: '.a = [\x00\x01\x05\x00ab;\n.b = 2;\n' },
  ];
  for (const { name, text } of cases) {
    const bad = checkStream(P, text);
    ok(bad.length === 0, `octet ${name}: ${bad.join('; ')}`);
    const types = [];
    P.parseBovnar(text, (ev) => types.push(ev.type));
    const starts = types.filter(t => t === P.EV.OCTET_STREAM_START).length;
    const ends = types.filter(t => t === P.EV.OCTET_STREAM_END).length;
    ok(starts === 1 && ends === 1,
       `octet ${name}: ${starts} start(s) and ${ends} end(s)`);
  }
}

/* A read that stops while a struct field's VALUE has not arrived (a fatal
   error in the field, a stream cut at that point) leaves the field's
   assignment pending when drain() closes the document. drain() popped the
   struct without closing it first, so struct_end closed the field's
   assignment and the enclosing assignment_end closed the struct: every level
   below was off by one. The live struct_close path already flushes first. */
console.log('# a stream cut short inside a struct closes every level');
{
  const cases = [
    '.a = {.x = <uint:8> 999;\n',
    '.a = {.x = <uint:8> 999;',
    '.a = {.x = @;\n',
    '.a = {.x = 1;\n',
    '.a = {.x = <uint:8> 999;\n.b = 2;\n',
  ];
  for (const text of cases) {
    const bad = checkStream(P, text);
    ok(bad.length === 0, `truncated ${JSON.stringify(text)}: ${bad.join('; ')}`);
  }
  /* The same invariant over generated documents corrupted at random — the
     shape that found the drain() bug above. */
  let seed = 9;
  const rnd = () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff;
  const ri = (n) => Math.floor(rnd() * n);
  const atoms = ['1', '-2', '3.5', '"x"', 'true', 'null', 'sym', '&.r',
    '1 m', '<uint:8> 7', '<float_fix:16,q8,°C> 1.5', '@',
    '\x00\x01\x02\x00ab;\x00'];
  function gen(d) {
    if (d > 3) return '1';
    const k = ri(8);
    if (k < 5) return atoms[ri(atoms.length)];
    if (k === 5)
      return '[' + Array.from({ length: ri(3) }, () => gen(d + 1)).join(', ') + ']';
    if (k === 6)
      return '{' + Array.from({ length: ri(3) },
        (_, i) => '.f' + i + ' = ' + gen(d + 1) + ';').join(' ') + '}';
    return '[' + gen(d + 1) + '] / [' + gen(d + 1) + ']';
  }
  for (let n = 0; n < 200; n++) {
    let text = '';
    for (let i = 0, k = 1 + ri(3); i < k; i++)
      text += '.k' + i + ' = ' + gen(0) + ';\n';
    if (rnd() < 0.4) {
      const at = ri(text.length);
      text = text.slice(0, at) + text.slice(at + 1);   /* drop one byte */
    }
    const bad = checkStream(P, text);
    ok(bad.length === 0,
       `balance ${n}: ${bad.join('; ')} in ${JSON.stringify(text)}`);
    const counts = eventCountDiff(P, bvnr.events(text).events, text);
    ok(counts.length === 0,
       `events ${n}: ${counts.join('; ')} in ${JSON.stringify(text)}`);
  }
}

console.log('# no assignment is rendered without its key');
for (const { name, text } of [...examples, ...shapes]) {
  /* `·null` in the tree is the signature of a value that was attached to the
     enclosing scope instead of to the container it belongs to. */
  const keys = [];
  (function walk(nodes) {
    for (const a of nodes) {
      keys.push(a.key);
      if (a.value && a.value.type === 'struct') walk(a.value.children);
    }
  })(P.parseFaithful(text).tree.children);
  ok(keys.every(k => typeof k === 'string' && k.length > 0),
     `keys ${name}: ${show(keys)}`);
}

console.log('# an assignment after a resynced error is still an assignment');
for (const { name, text, after } of resync) {
  const got = P.parseFaithful(text).tree.children.map(a => a.key);
  ok(show(got) === show(after), `resync ${name}: top level is ${show(got)}, expected ${show(after)}`);
}

/* The document tier is a second full pass with no line:column to report, so it
   is opt-in: the playground asks for it, and the demos — which re-parse a
   freshly encoded frame every animation step — must not be charged for it or
   handed a finding they would show at line 0. Both halves are pinned here
   because that division has now been got wrong in both directions. */
console.log('# document-tier violations reach the on_error channel, when asked for');
for (const { name, text } of docTier) {
  const cli = cliValidate(text);
  ok(!cli.ok, `${name}: the CLI accepts it — the case no longer tests what it claims`);
  const errs = P.parseFaithful(text, { documentTier: true }).errors;
  ok(errs.some(e => e.code === cli.error_name),
     `error ${name}: cli=${cli.error_name} shim=${show(errs.map(e => e.code))}`);
  const plain = P.parseFaithful(text).errors;
  ok(!plain.some(e => e.tier === 'document'),
     `error ${name}: reported without documentTier: ${show(plain.map(e => e.code))}`);
}

/* bvn_dom_doc_get_parse_error is a code with no site, but each of these rules is
   a property of a node in the tree, so the shim recovers one — without it the
   playground can only say "document" and offer no jump to the offending key. */
console.log('# a document-tier violation points at the node it is about');
for (const { name, text, at } of docTier) {
  const e = P.parseFaithful(text, { documentTier: true }).errors
    .find(x => x.tier === 'document');
  ok(e && e.line === at[0] && e.col === at[1],
     `site ${name}: ${e ? e.line + ':' + e.col : 'none'}, expected ${at[0]}:${at[1]}`);
}

console.log('# a document the CLI accepts reports no errors');
for (const { name, text } of [...examples, ...shapes]) {
  if (!cliValidate(text).ok) continue;
  const errs = P.parseFaithful(text, { documentTier: true }).errors;
  ok(errs.length === 0, `clean ${name}: ${show(errs.map(e => e.code))}`);
}

/*
 * The elements of one array may each carry their own annotation (§7.5), and
 * their encodings are free to differ as long as the dimension agrees (§7.4), so
 * `[<uint:8> 1, <sint:16> -2, <float:64> 3.5]` — a document the CLI accepts —
 * carries three. The shim used to assume there was only ever one: it hoisted the
 * first element's onto the assignment and dropped the rest, so the playground
 * presented that array as a uint:8, a type neither -2 nor 3.5 has, in a tree
 * whose own badge counted the 20 events it had just been handed.
 *
 * The rule that replaced it is about PLACEMENT, so that is what this checks: an
 * annotation appears where the reader emitted it and nowhere else. One written
 * before the '[' describes the array and belongs to the assignment; one emitted
 * inside a row belongs to the element it precedes. Hoisting an in-row annotation
 * to the assignment because it happens to govern every element would put the
 * same event in two different places depending on the data.
 *
 * Nothing here is a literal: the expectation is walked out of the C stream. Its
 * container events (row start/end, struct open/close) say exactly which `data`
 * events are array ELEMENTS — a struct field inside an element is not one — and
 * an annotation binds to the next element after it. The reader also says which
 * annotations it invented: a synthesised one always spells out a type_base of 0,
 * "no explicit base", which no written annotation can produce because `_0` is
 * not a radix anyone may write.
 *
 * With ONE exception, pinned below rather than left to drift. `<utf8>` and
 * `<bool>` are whole annotations with no parameters at all, and the reader emits
 * the identical group whether the document wrote one or not — `.a = "x";` and
 * `.a = <utf8> "x";` are the same eleven events. So for those two families the
 * stream carries neither parameters to show nor any way to tell synthesised from
 * explicit, and the tree renders no annotation node rather than invent a tag the
 * C never stated. The values still carry their family beside them.
 */
console.log('# every annotation sits on the element the C emitted it for');
{
  /* What the C says: the per-element annotations in document order, whether an
     array-level one preceded the first row, and the distinct element types. */
  function streamAnns(text) {
    const evs = bvnr.events(text).events || [];
    const stack = [], elements = [], types = new Set();
    let pending = null, arrayLevel = null, sawRow = false, bare = 0;
    for (const e of evs) {
      switch (e.ev) {
        case 'type_start':  pending = { synth: false, params: 0 }; break;
        case 'type_param':
          if (!pending) break;
          pending.params++;
          /* the reader's "no explicit base" — only it can emit this */
          if (e.tok === 'type_base' && !e.base) pending.synth = true;
          break;
        /* a parameterless group is `<utf8>`/`<bool>` — see the note above */
        case 'type_end':    if (pending && !pending.params) { bare++; pending = null; }
                            break;
        case 'array_row_start':
          if (pending && !sawRow) arrayLevel = pending;
          pending = null; sawRow = true; stack.push('row'); break;
        case 'array_row_end':  stack.pop(); break;
        case 'struct_open':    stack.push('struct'); break;
        case 'struct_close':   stack.pop(); break;
        case 'data':
          if (e.tok === 'octet_stream') break;
          /* an element only when the innermost container is a row — a struct
             field inside an element is that struct's, not the array's */
          if (stack[stack.length - 1] === 'row') {
            elements.push(pending ? { synth: pending.synth } : null);
            if (e.family) types.add(`${e.family}:${e.width},${e.unit || ''}`);
          }
          pending = null;
          break;
      }
    }
    return { elements, arrayLevel, types, bare };
  }
  /* What the tree says: the same elements, in the same order. */
  function treeAnns(value, out) {
    if (value.type === 'struct') {
      for (const a of value.children) treeAnns(a.value, out);
      return out;
    }
    if (value.type !== 'array') return out;
    for (const row of value.rows)
      for (const e of row.elements) {
        if (e.type === 'array' || e.type === 'struct') treeAnns(e, out);
        else out.push(e.ann ? { synth: e.ann.synthesized } : null);
      }
    return out;
  }
  const annCases = [
    { name: 'mixed encodings',        text: '.a = [<uint:8> 1, <sint:16> -2, <float:64> 3.5];\n' },
    { name: 'bare array',             text: '.a = [1,2,3];\n' },
    { name: 'array-level annotation', text: '.a = <uint:8> [1,2,3];\n' },
    { name: 'per-element, agreeing',  text: '.a = [<uint:8> 1, <uint:8> 2];\n' },
    { name: 'per-element, reverting', text: '.a = [<uint:8> 1, 2];\n' },
    { name: 'united elements',        text: '.a = [<float:64,m> 1.0, <float:64,m> 2.0];\n' },
    { name: 'matrix, one type',       text: '.m = [1,2]/[3,4];\n' },
    { name: 'matrix, mixed row',      text: '.m = [<uint:8> 1,2]/[3,4];\n' },
    { name: 'null hole',              text: '.a = [ , 1];\n' },
    { name: 'nested, mixed',          text: '.a = [[<uint:8> 1,2],[3,4]];\n' },
    /* `bare` marks the pinned exception: these documents DO carry annotation
       groups, parameterless ones, and the tree deliberately shows none. */
    { name: 'strings, no annotation', bare: true, text: '.a = ["x","y"];\n' },
    { name: 'bools, no annotation',   bare: true, text: '.a = [true,false];\n' },
    { name: 'written <utf8>',         bare: true, text: '.a = [<utf8> "x", "y"];\n' },
    { name: 'structs as elements',    text: '.a = [{.b = <uint:8> 3;},{.b = <uint:8> 4;}];\n' },
    { name: 'array inside a struct',  text: '.s = {.q = [<uint:8> 1, <sint:16> -2];};\n' },
  ];
  for (const { name, text, bare } of annCases) {
    ok(cliValidate(text).ok, `${name}: the CLI rejects it — the case tests nothing`);
    const want = streamAnns(text);
    ok((want.bare > 0) === !!bare,
       `ann ${name}: the C emits ${want.bare} parameterless group(s), `
       + `so this case ${bare ? 'no longer' : 'now'} exercises the pinned exception`);
    const a = P.parseFaithful(text).tree.children[0];
    const arr = a && a.value && a.value.type === 'array' ? a.value : null;

    /* the whole of it: element for element, annotation for annotation */
    const got = treeAnns(a.value, []);
    ok(show(got) === show(want.elements),
       `ann ${name}: elements carry ${show(got)}, the C emitted ${show(want.elements)}`);

    /* and the assignment shows one only where the document wrote one there */
    ok(!!(a.ann) === !!want.arrayLevel,
       `ann ${name}: assignment ${a.ann ? 'shows' : 'shows no'} annotation, `
       + `the C emitted ${want.arrayLevel ? 'an array-level one' : 'none before the row'}`);
    if (a.ann && want.arrayLevel) {
      ok(a.ann.synthesized === want.arrayLevel.synth,
         `ann ${name}: array-level tagged ${a.ann.synthesized ? 'synthesised' : 'explicit'}, `
         + `the C says the other`);
      /* and it must be a type the C actually resolved, not a near miss */
      const spelled = `${a.ann.familyName}:`
        + `${(a.ann.params.find(p => p.kind === 'width') || {}).text},`
        + `${((a.ann.params.find(p => p.kind === 'unit') || {}).text || '')
             .replace('no_unit', '')}`;
      ok(want.types.has(spelled),
         `ann ${name}: shows ${show(spelled)}, C reports ${show([...want.types])}`);
    }

    /* mixedTypes is the row summary's hint, and must agree with the C's own
       count of distinct element types. The key drops the radix on purpose: it is
       not part of what the tree displays (the "no explicit base" 0 resolves to
       decimal, as the CLI prints it), so `[1, <uint:64,_10> 2]` is one type and
       must not be called mixed over a distinction nothing renders. */
    if (arr)
      ok(arr.mixedTypes === (want.types.size > 1),
         `ann ${name}: mixedTypes ${arr.mixedTypes}, but the C reports `
         + `${want.types.size} element type(s): ${show([...want.types])}`);
  }
  /* A scalar's annotation is the assignment's, and reads its tag from the same
     place — it must keep answering the way the regex it replaced did for the
     shapes that regex could actually see. */
  for (const [text, synth] of [['.a = 1;\n', true], ['.a = <uint:8> 1;\n', false],
                               ['.a = 9.81 m/s;\n', true],
                               ['.a = <float:32,k~Pa> 101.325;\n', false]]) {
    const ann = P.parseFaithful(text).tree.children[0].ann;
    ok(ann && ann.synthesized === synth,
       `ann scalar ${show(text.trim())}: ${ann ? ann.synthesized : 'no annotation'}, expected ${synth}`);
  }
}

/* A resynced assignment never delivers its value, so nothing claims the
   annotation the reader emitted for it. That pending annotation used to survive
   into the NEXT assignment and wrap itself around an array the document never
   annotated — `.a = <uint:8> 999; .b = [1,2];` showed `.b` as explicitly
   `<uint>`, an event the reader never emitted for it. The tree must show the
   annotations the reader delivered and no others. */
console.log('# a skipped assignment does not leak its annotation');
{
  const cases = [
    { name: 'top-level array', text: '.a = <uint:8> 999; .b = [1,2];\n', path: ['b'] },
    { name: 'array in a struct', text: '.a = <uint:8> 999; .s = {.x = [1,2];};\n',
      path: ['s', 'x'] },
  ];
  for (const { name, text, path } of cases) {
    const r = P.parseFaithful(text);
    ok(r.errors.length > 0, `leak ${name}: premise — the value is refused`);
    let assign = r.tree.children.find(a => a.key === path[0]);
    for (const key of path.slice(1))
      assign = assign && assign.value.type === 'struct'
        ? assign.value.children.find(a => a.key === key) : null;
    const arr = assign && assign.value;
    ok(arr && arr.type === 'array', `leak ${name}: the array is in the tree`);
    ok(arr && !arr.ann && !(assign && assign.ann),
       `leak ${name}: no phantom annotation (assignment ${show(assign && assign.ann)}, `
       + `array ${show(arr && arr.ann)})`);
    /* ...and the element annotation the C DID emit for this array still lands */
    ok(arr && arr.rows[0].elements[0].ann &&
       arr.rows[0].elements[0].ann.synthesized === true,
       `leak ${name}: the element keeps its own synthesised annotation`);
  }
  /* An annotation the document really wrote after the skipped one still lands,
     explicit, from its own event group. */
  const good = P.parseFaithful('.a = <uint:8> 999; .b = <sint:16> [1,2];\n');
  const b = good.tree.children.find(a => a.key === 'b');
  ok(b && b.ann && b.ann.synthesized === false && b.ann.familyName === 'sint',
     `leak: a written annotation still lands ${show(b && b.ann)}`);
}

/* The annotation's resolved view must not invent parameters the format cannot
   write. Only uint/sint/float have a base parameter; a datetime's slot holds
   the epoch (a `_10` there is not even writable), and float_fix/float_dec have
   no base parameter at all. The synthesised flag for a bare datetime comes from
   the group's text and parameter count: the reader emits no type_base for it,
   so the old signal could not see the default it invented. */
console.log('# annotation parameters the format can actually write');
{
  const annOf = (text) => P.parseFaithful(text).tree.children[0].ann;
  const kinds = (a) => (a && a.params ? a.params.map(p => p.kind) : []);
  const bare = annOf('#!bovnar 1.1\n.t = 2026-06-15T12:00:00Z;');
  ok(bare && bare.synthesized === true,
     `datetime default is tagged synthesised ${show(bare)}`);
  ok(bare && !kinds(bare).includes('base') &&
     bare.params.some(p => p.kind === 'epoch' && p.text === 'unix'),
     `datetime default has no base parameter ${show(bare)}`);
  const explicit = annOf('#!bovnar 1.1\n.t = <datetime:64,tai> 2026-06-15T12:00:00Z;');
  ok(explicit && explicit.synthesized === false &&
     !kinds(explicit).includes('base') &&
     explicit.params.some(p => p.kind === 'epoch' && p.text === 'tai'),
     `explicit datetime annotation ${show(explicit)}`);
  const noWidth = annOf('#!bovnar 1.1\n.t = <datetime> 2026-06-15T12:00:00Z;');
  ok(noWidth && noWidth.synthesized === false,
     `explicit <datetime> is not called synthesised ${show(noWidth)}`);
  const ff = annOf('.v = <float_fix:16,q8,°C> 1.5;');
  ok(ff && !kinds(ff).includes('base') &&
     ff.params.some(p => p.kind === 'q' && p.value === 8),
     `float_fix keeps q, not a base ${show(ff)}`);
  const fd = annOf('.v = <float_dec:64,$USD> 1.5;');
  ok(fd && !kinds(fd).includes('base'),
     `float_dec has no base parameter ${show(fd)}`);
  const ui = annOf('.v = 1;');
  ok(ui && ui.params.some(p => p.kind === 'base' && p.text === '_10'),
     `uint still shows the resolved _10 ${show(ui)}`);
}

/* A float_fix annotation's q is a parameter of its own, not a radix. The C
   event carries "q": 8; the shim used to read a `text` the event never had, so
   parseBovnar rendered {kind:'q', text:undefined}. Pinned against the C so a
   further shuffle of the parameter fields cannot silently blank it. */
console.log('# a float_fix q parameter reaches the translated stream');
{
  const evs = [];
  P.parseBovnar('.v = <float_fix:16,q8> 1.5;', (ev) => {
    if (ev.type === P.EV.TYPE_ANN_PARAM) evs.push(ev.data);
  });
  const c = bvnr.events('.v = <float_fix:16,q8> 1.5;').events
    .filter(e => e.ev === 'type_param' && e.tok === 'type_q');
  ok(c.length === 1 && c[0].q === 8 && !('base' in c[0]),
     `C q param: ${show(c)}`);
  const q = evs.find(x => x.kind === 'q');
  ok(q && q.text === 'q8' && q.value === 8,
     `shim q param: ${show(evs)}`);
}

/* The unit parameter is part of the annotation, and `no_unit` is a value of it:
   `<uint:8>` carries none, `<uint:8,no_unit>` carries that one, and a bare
   value's synthesised annotation spells it out. The translator used to drop it
   as a "sentinel", losing the event and making the three indistinguishable. */
console.log('# every type parameter the reader emitted reaches the callback');
{
  const paramsOf = (text) => {
    const out = [];
    P.parseBovnar(text, (ev) => {
      if (ev.type === P.EV.TYPE_ANN_PARAM) out.push(ev.data);
    });
    return out;
  };
  const bare = paramsOf('.a = 1;');
  ok(bare.some(p => p.kind === 'unit' && p.text === 'no_unit'),
     `a synthesised annotation keeps its unit parameter ${show(bare)}`);
  const omitted = paramsOf('.a = <uint:8> 1;');
  ok(!omitted.some(p => p.kind === 'unit'),
     `an omitted unit parameter is not invented ${show(omitted)}`);
  const explicit = paramsOf('.a = <uint:8,no_unit> 1;');
  ok(explicit.some(p => p.kind === 'unit' && p.text === 'no_unit'),
     `an explicit no_unit reaches the callback ${show(explicit)}`);
}

/* An octet stream is raw binary, and a payload may contain '"', '#' or a
   '.key =' byte sequence. The scanner masks string and comment spans so it can
   find keys; before it masked octet payloads too, a quote inside one put the
   whole rest of the document "inside a string" and every later assignment got
   the fallback line, and a key-like payload could steal the next key's site. */
console.log('# keys after an octet payload keep their lines');
{
  const cases = [
    { name: 'quote and hash in the payload',
      text: '.bin = \x00\x01\x05\x00a"b#c\x01\x03\x00x.y\x00;\n.after = 1;\n.next = 2;\n',
      want: [['bin', 1], ['after', 2], ['next', 3]] },
    { name: 'a key-like payload',
      text: '.bin = \x00\x01\x0a\x00.next = 9;\x00;\n.after = 1;\n',
      want: [['bin', 1], ['after', 2]] },
    { name: 'a newline in the payload is binary, not a line',
      text: '.bin = \x00\x01\x03\x00a\nb\x00;\n.after = 1;\n',
      want: [['bin', 1], ['after', 2]] },
    // The reader's display column counts a whole binary region as ONE column
    // (its opener's), so a key after one on the same line is not pushed right
    // by the payload's length. `@` in `.after = @` is column 20 in the C;
    // `.after` therefore starts at 11.
    { name: 'a key after an octet on the same line',
      text: '.bin = \x00\x01\x03\x00abc\x00; .after = 1;\n',
      want: [['bin', 1], ['after', 1]], cols: [1, 11] },
    { name: 'an escaped quote is still a string',
      text: '.s = "a\\"b";\n.after = 1;\n',
      want: [['s', 1], ['after', 2]] },
  ];
  for (const { name, text, want, cols } of cases) {
    const r = P.parseFaithful(text);
    const got = r.tree.children.map(a => [a.key, a.line]);
    ok(show(got) === show(want), `octet scan ${name}: ${show(got)} expected ${show(want)}`);
    if (cols) {
      const gotCols = r.tree.children.map(a => a.col);
      ok(show(gotCols) === show(cols),
         `octet scan ${name}: columns ${show(gotCols)} expected ${show(cols)}`);
    }
    ok(r.errors.length === 0,
       `octet scan ${name}: no errors (${show(r.errors.map(e => e.code))})`);
  }
}

/* Randomized: payload bytes that are legal inside a chunk but would confuse a
 * text scanner if it were naive (0x0A/0x0D newline and carriage return, quotes,
 * '#' comments, dots, '=', spaces) must not move the keys that follow. Bytes are
 * kept to ASCII without 0x00: a 0x00 ends the chunk on the wire, and a non-ASCII
 * byte is two UTF-8 bytes while a textarea holds one character (the shim's
 * documented limit for binary input). */
console.log('# random octet payloads do not move later keys');
{
  let seed = 1;
  const rnd = () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff;
  const alphabet = [0x01, 0x0a, 0x0d, 0x22, 0x23, 0x2e, 0x3d, 0x41, 0x20, 0x7f];
  for (let n = 0; n < 100; n++) {
    const len = 1 + Math.floor(rnd() * 12);
    let payload = '';
    for (let i = 0; i < len; i++)
      payload += String.fromCharCode(alphabet[Math.floor(rnd() * alphabet.length)]);
    const text = `.bin = \x00\x01${String.fromCharCode(len)}\x00${payload}\x00;\n` +
                 '.k0 = 1;\n.k1 = 2;\n';
    const r = P.parseFaithful(text);
    const got = r.tree.children.map(a => [a.key, a.line]);
    ok(show(got) === show([['bin', 1], ['k0', 2], ['k1', 3]]),
       `octet scan random ${n}: ${show(got)} for ${JSON.stringify(payload)}`);
    ok(r.errors.length === 0,
       `octet scan random ${n}: no errors (${show(r.errors.map(e => e.code))})`);
  }
}

/* Randomized mixing of every masked span: strings with escaped quotes, '#'
 * comments, octet streams and same-line structs. Each top-level assignment
 * sits on its own line, so its line is known by construction; if any masker
 * path missed or over-ran a character, the offsets and every later line
 * would drift. */
console.log('# comments, strings and octets keep the line map');
{
  let seed = 7;
  const rnd = () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff;
  const pick = (a) => a[Math.floor(rnd() * a.length)];
  for (let n = 0; n < 60; n++) {
    const lines = [];
    const keys = [];
    if (rnd() < 0.5) lines.push('# a comment with .fake = 1; and "quotes"');
    const count = 2 + Math.floor(rnd() * 4);
    for (let i = 0; i < count; i++) {
      const key = `k${n}_${i}`;
      const kind = Math.floor(rnd() * 5);
      let value;
      if (kind === 0) value = `"a\\"b#c\\\\d"`;
      else if (kind === 1) value = 'sym';
      else if (kind === 2) {
        const len = 1 + Math.floor(rnd() * 6);
        let p = '';
        for (let k = 0; k < len; k++) p += pick(['\x0a', '"', '#', '.', '=', 'A', ' ']);
        value = `\x00\x01${String.fromCharCode(len)}\x00${p}\x00`;
      } else if (kind === 3) value = '{.x = 1; .y = "q#";}';
      else value = String(Math.floor(rnd() * 1000));
      lines.push(`.${key} = ${value};`);
      keys.push([key, lines.length]);
    }
    const r = P.parseFaithful(lines.join('\n') + '\n');
    const got = r.tree.children.map(a => [a.key, a.line]);
    ok(show(got) === show(keys),
       `line map ${n}: ${show(got)} expected ${show(keys)}`);
    ok(r.errors.length === 0,
       `line map ${n}: no errors (${show(r.errors.map(e => e.code))}) in ` +
       JSON.stringify(lines.join('\n')));
  }
}

/* Spelled out separately from the differential above, which can only compare
   documents the DOM accepts — and the version of this that started it all,
   `.a = [{.b=3;},{.c=4;}];`, is rejected. Every read here is guarded: on the
   walk this guards against, none of these nodes are the shape they claim. */
console.log('# the regression itself');
{
  const t = P.parseFaithful('.a = [{.b=3;},{.b=4;}];\n').tree;
  const a = t.children[0];
  const arr = a && a.value && a.value.type === 'array' ? a.value : null;
  const row = arr && arr.rows ? arr.rows[0] : null;
  const els = row ? row.elements : null;
  ok(t.children.length === 1,
     `array of structs produced ${t.children.length} top-level assignment(s), expected 1`);
  ok(a && a.key === 'a' && arr,
     `.a is ${a ? show(a.key) + '/' + (a.value && a.value.type) : 'missing'}, expected "a"/array`);
  ok(els && els.length === 2 && els.every(e => e.type === 'struct'),
     `the row holds ${els ? show(els.map(e => e.type)) : 'nothing'}, expected two structs`);
  ok(arr && arr.rows.length === 1 && arr.dims === 1,
     `array reports ${arr && arr.rows.length} row(s) / dims ${arr && arr.dims}, expected 1 / 1`);
}

/*
 * The page promises that every frame of the three demos and everything the
 * playground shows is parsed by the C reference implementation, not a
 * JavaScript re-implementation. That holds only while every link of the chain
 * is the current one: each demo's decoder (and the playground) calls
 * BovnarParser.parseFaithful; the shim imports the cache-stamped wrapper that
 * is on disk; the wrapper imports the current core; and the page loads and
 * preloads those same stamped files. This is the wiring the shim tests above
 * cannot see — they replace the import and run the shim standalone.
 */
console.log('# the website demos and playground run on this shim');
{
  const page = readFileSync(join(ROOT, 'web', 'index.html'), 'utf8');
  const shimPath = join(ROOT, 'web', 'bovnar_parser_wasm.js');
  const shimSrc = readFileSync(shimPath, 'utf8');
  const hash = (p) => createHash('sha256').update(readFileSync(p)).digest('hex').slice(0, 12);

  /* (a) every decoder in the page goes through the shim. The three demo decode
     helpers are identical: parse, then project the tree. */
  const decoders = [...page.matchAll(/function decode\(text[^)]*\)\s*\{([\s\S]{0,4000}?)\n  \}/g)];
  ok(decoders.length === 3, `page: ${decoders.length} demo decoder(s), expected 3`);
  decoders.forEach((m, i) => {
    ok(/const res = parseFaithful\(text, opts\);/.test(m[1]),
       `page: demo decoder ${i} does not parse through parseFaithful`);
  });
  ok(/const parseF = BovnarParser\.parseFaithful;/.test(page),
     'page: the playground does not bind BovnarParser.parseFaithful');
  /* the event tree draws the stream end only when the reader delivered one */
  ok(/res\.events\.some\(\(e\) => e\.ev === 'stream_end'\)/.test(page),
     'page: the playground appends a stream_end the reader did not deliver');
  /* A decoded datetime is its epoch-second carrier, not an ISO string: the
     mission MET must come from the frame's decoded .mission_time, never from
     Date.parse() of the stamp (which is NaN and silently used the sim clock). */
  ok(/tmm\.mission_time/.test(page) && !/metFromStamp\(/.test(page),
     'page: mission MET does not use the decoded mission_time');

  /* The config editors are the one place a demo validates user-typed text, so
     they ask for the document tier (duplicate keys, array shapes); the frames
     keep the single-pass streaming decode for speed. */
  const cfgDecode = {
    cfg: /decode\(txt, \{ documentTier: true \}\)/,
    fcfg: /decode\(\$\('fcfg'\)\.value, \{ documentTier: true \}\)/,
    icfg: /decode\(\$\('icfg'\)\.value, \{ documentTier: true \}\)/,
  };
  for (const [id, re] of Object.entries(cfgDecode))
    ok(re.test(page), `page: #${id} does not get the two-tier verdict`);

  /* (b) the shim imports the wrapper on disk, and the wrapper the core, each
     under the cache stamp build_wasm.sh computes from the file's bytes. */
  const wrapHash = hash(join(ROOT, 'web', 'bovnar_wasm.js'));
  const coreHash = hash(join(ROOT, 'web', 'bovnar_wasm_core.js'));
  ok(shimSrc.includes(`./bovnar_wasm.js?v=${wrapHash}`),
     `shim: imports a wrapper other than bovnar_wasm.js?v=${wrapHash}`);
  const wrapSrc = readFileSync(join(ROOT, 'web', 'bovnar_wasm.js'), 'utf8');
  ok(wrapSrc.includes(`./bovnar_wasm_core.js?v=${coreHash}`),
     `wrapper: imports a core other than bovnar_wasm_core.js?v=${coreHash}`);

  /* (c) the page loads that shim and preloads the same chain. */
  ok(page.includes(`bovnar_parser_wasm.js?v=${hash(shimPath)}`),
     'page: does not load the current bovnar_parser_wasm.js stamp');
  ok(page.includes(`bovnar_wasm.js?v=${wrapHash}`) &&
     page.includes(`bovnar_wasm_core.js?v=${coreHash}`),
     'page: modulepreload does not name the current wrapper/core stamps');
}

// --- report ----------------------------------------------------------------
console.log(`\n${pass} passed, ${fail} failed`);
if (fail) { console.log('\nFAILURES:'); for (const f of fails) console.log('  ✗ ' + f); process.exit(1); }
console.log('ALL CHECKS PASSED ✓');
