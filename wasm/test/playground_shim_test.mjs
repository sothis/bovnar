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
import { readFileSync, readdirSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const CLI = process.env.BOVNAR_CLI || join(ROOT, 'build', 'bovnar');
const SHIM = join(ROOT, 'web', 'bovnar_parser_wasm.js');
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
  { name: 'struct shape mismatch', text: '.a = [{.b=3;},{.c=4;}];\n' },
  { name: 'duplicate key',         text: '.x = 1;\n.x = 2;\n' },
  { name: 'heterogeneous array',   text: '.mix = [1,"two"];\n' },
  { name: 'ragged nested array',   text: '.r = [[1,2],[3,4,5]];\n' },
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

console.log('# every assignment is closed exactly once, by its own value');
for (const { name, text } of [...examples, ...shapes, ...resync]) {
  const bad = checkStream(P, text);
  ok(bad.length === 0, `events ${name}: ${bad.join('; ')}`);
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

console.log('# a document the CLI accepts reports no errors');
for (const { name, text } of [...examples, ...shapes]) {
  if (!cliValidate(text).ok) continue;
  const errs = P.parseFaithful(text, { documentTier: true }).errors;
  ok(errs.length === 0, `clean ${name}: ${show(errs.map(e => e.code))}`);
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

// --- report ----------------------------------------------------------------
console.log(`\n${pass} passed, ${fail} failed`);
if (fail) { console.log('\nFAILURES:'); for (const f of fails) console.log('  ✗ ' + f); process.exit(1); }
console.log('ALL CHECKS PASSED ✓');
