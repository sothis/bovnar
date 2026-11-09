// Differential + smoke test for the Bovnar WASM module.
//
// Compares the WebAssembly build against the native `bovnar` CLI (the reference)
// across the example corpus and a set of deliberately malformed snippets:
//
//   validate : HARD assertion — WASM and the CLI must agree on ok/error_name.
//   toJSON   : SOFT check — JSON.parse deep-equality (neutralises float
//              formatting differences); mismatches are reported, not fatal,
//              since the projection intentionally differs for some kinds.
//
// Run:  node wasm/test/diff_test.mjs           (from the repo root)
// Requires: a built dist/wasm/ and a built ./build/bovnar CLI.

import { loadBovnar } from '../../dist/wasm/index.mjs';
import { execFileSync } from 'node:child_process';
import { readFileSync, readdirSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
// Honour BOVNAR_CLI so the test runner can point at a non-default --build-dir;
// fall back to the conventional ./build/bovnar.
const CLI = process.env.BOVNAR_CLI || join(ROOT, 'build', 'bovnar');
const tmp = mkdtempSync(join(tmpdir(), 'bvnr-diff-'));
// Remove the scratch dir on any exit path (success, failure, or throw) so runs
// don't leak a bvnr-diff-* dir into $TMPDIR.
process.on('exit', () => { try { rmSync(tmp, { recursive: true, force: true }); } catch {} });

let pass = 0, fail = 0, soft = 0;
const fails = [];
function ok(cond, msg) { if (cond) pass++; else { fail++; fails.push(msg); } }

// --- CLI helpers -----------------------------------------------------------
function cliValidate(text, extra = []) {
  const f = join(tmp, 'in.bvnr');
  writeFileSync(f, text);
  try {
    execFileSync(CLI, ['validate', ...extra, f], { encoding: 'utf8' });
    return { ok: true, error_name: 'none' };
  } catch (e) {
    const out = (e.stdout || '') + (e.stderr || '');
    const m = out.match(/Validation failed:\s*(\w+)/);
    return { ok: false, error_name: m ? m[1] : '?' };
  }
}
/* `bovnar query [opts] <path> <file>` -- the CLI's DOM-under-a-unit-policy
 * surface, which is what toJSON({policy}) and doc({policy}) project. */
function cliQuery(text, path, extra = []) {
  const f = join(tmp, 'in.bvnr');
  writeFileSync(f, text);
  try {
    const out = execFileSync(CLI, ['query', ...extra, path, f],
                             { encoding: 'utf8' });
    return { ok: true, out: out.trim() };
  } catch (e) {
    return { ok: false, out: ((e.stdout || '') + (e.stderr || '')).trim() };
  }
}
function throws(fn) {
  try { fn(); return false; } catch { return true; }
}
/* `convert` exits 1 when the JSON it wrote cannot be converted back to the same
   document -- a unit, a wide integer, and (since the report learned of them) a
   datetime or a non-finite float. The JSON is still on stdout, and for the value
   cases below it is exactly what toJSON() is being compared with, so `lossy`
   takes it from the failed run. Without it a lossy document has no CLI value,
   which is what the example loop wants. */
function cliJson(text, { lossy = false } = {}) {
  const f = join(tmp, 'in.bvnr');
  writeFileSync(f, text);
  try {
    return JSON.parse(execFileSync(CLI, ['convert', f],
                                   { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }));
  } catch (e) {
    if (lossy && e.status === 1 && /LOSSY/.test(e.stderr || '')) {
      try { return JSON.parse(e.stdout); } catch { return undefined; }
    }
    return undefined;
  }
}

function deepEq(a, b) {
  if (a === b) return true;
  if (typeof a !== typeof b) return false;
  if (a && b && typeof a === 'object') {
    const ka = Object.keys(a), kb = Object.keys(b);
    if (ka.length !== kb.length) return false;
    return ka.every(k => deepEq(a[k], b[k]));
  }
  return false;
}

// --- corpus ----------------------------------------------------------------
const exDir = join(ROOT, 'examples');
const examples = readdirSync(exDir).filter(f => f.endsWith('.bvnr'))
  .map(f => ({ name: f, text: readFileSync(join(exDir, f), 'utf8') }));

const badCases = [
  ['unit mismatch', '.v = <float:64,m/s> 9.81 kg;\n'],
  ['type/value mismatch', '.v = <uint:8> "hello";\n'],
  ['out of range', '.v = <uint:8> 999;\n'],
  ['digit not in base', '.v = <uint:16,_2> 9;\n'],
  ['unterminated struct', '.v = { .a = 1;\n'],
  ['empty identifier', '. = 1;\n'],
  ['bad escape', '.v = "bad\\q";\n'],
  ['1.1 construct in 1.0 doc', '.t = <datetime:64,unix> 1750000000;\n'],
];

// Values whose correctness the CLI and the module must agree on -- chosen so a
// stale module shows up. The tai leap second is the one that caught the drift.
const valueCases = [
  { name: 'tai inserted leap second',
    text: '#!bovnar 1.1\n.t = <datetime:64,tai> 2016-12-31T23:59:60Z;\n' },
  { name: 'the second after it',
    text: '#!bovnar 1.1\n.t = <datetime:64,tai> 2017-01-01T00:00:00Z;\n' },
  { name: 'leap second with a tz offset',
    text: '#!bovnar 1.1\n.t = <datetime:64,tai> 2017-01-01T00:59:60+01:00;\n' },
  { name: 'civil epoch still collapses :60',
    text: '#!bovnar 1.1\n.t = <datetime:64,unix> 2016-12-31T23:59:60Z;\n' },
  /* Units, currencies and >64-bit integers are deliberately NOT here: the CLI
     refuses to convert those to JSON rather than silently dropping the unit or
     the precision, so there is no CLI value to compare against. They are covered
     by the validate cases above. */
  { name: 'negative and fractional',
    text: '#!bovnar 1.1\n.a = -273.15;\n.b = <sint:64> -9007199254740993;\n' },
  { name: 'special floats',
    text: '#!bovnar 1.1\n.a = nan;\n.b = inf;\n.c = ninf;\n' },
  { name: 'string escapes',
    text: '#!bovnar 1.1\n.s = "tab\\there \\u{1F600} quote\\"end";\n' },
  { name: 'nested array and struct',
    text: '#!bovnar 1.1\n.a = [[1, 2], [3, 4]];\n.s = {.x = 1; .y = 2;};\n' },
];

// --- run -------------------------------------------------------------------
const b = await loadBovnar();
console.log(`bovnar-wasm ${b.version()}  vs  CLI @ ${CLI}\n`);

console.log('# valid examples');
for (const { name, text } of examples) {
  const w = b.validate(text);
  const c = cliValidate(text);
  ok(w.ok === c.ok && w.error_name === c.error_name,
     `validate ${name}: wasm=${w.ok}/${w.error_name} cli=${c.ok}/${c.error_name}`);

  // HARD: the values must agree too. This was a soft warning, which is why the
  // shipped module could drift 35 commits behind the library -- answering the
  // pre-fix value for a tai leap second -- without anything failing.
  if (w.ok && c.ok) {
    const wj = b.toJSON(text);
    const cj = cliJson(text);
    if (cj !== undefined && wj.ok) {
      ok(deepEq(wj.value, cj),
         `toJSON ${name}: wasm=${JSON.stringify(wj.value)} cli=${JSON.stringify(cj)}`);
    }
  }
}

console.log('# values (must match the CLI exactly)');
for (const { name, text } of valueCases) {
  const w = b.toJSON(text);
  const c = cliJson(text, { lossy: true });
  ok(w.ok && c !== undefined && deepEq(w.value, c),
     `value ${name}: wasm=${JSON.stringify(w.ok ? w.value : w.error)} cli=${JSON.stringify(c)}`);
}

console.log('# malformed snippets (must error, same code as CLI)');
for (const [label, text] of badCases) {
  const w = b.validate(text);
  const c = cliValidate(text);
  ok(!w.ok && w.error_name === c.error_name,
     `bad "${label}": wasm=${w.ok}/${w.error_name} cli=${c.ok}/${c.error_name}`);
}

/* eventsConvert — the entry point that does the actual unit work, and the one
   nothing compared against the CLI. It runs the reader in resync mode, so a
   value whose conversion the policy cannot deliver is SKIPPED; deriving `ok`
   from the reader's final code (cleared on a clean EOF after a resync) reported
   {"ok":true} with the data event simply absent — a stream carrying a key and
   no value, and nothing saying so. `bvnr_wasm_parse` had already learned this
   and computes ok from an error count; events_impl had not. */
console.log('# eventsConvert vs the CLI');
const convCases = [
  ['exact metric',        '.v = <float:64,k~m/h> 90.0;',  'm/s',      true ],
  ['non-terminating',     '.v = <float:64,k~m/h> 100.0;', 'm/s',      false],
  ['affine, lone',        '.v = <float:64,\u00b0C> 25.0;',    'K',        true ],
  ['affine in a compound','.v = <float:64,\u00b0C/h> 1.0;',   'K/h',      false],
  ['currency prefix',     '.v = <float:64,$USD> 5.0;',    'k~$USD',   true ],
  ['inline unit',         '.v = 9.81 k~m/s;',             'm/s',      true ],
  ['unknown target',      '.v = <float:64,m> 1.0;',       'nosuchme', false],
];
for (const [label, text, unit, wantOk] of convCases) {
  const w = b.eventsConvert(text, unit);
  ok(w.ok === wantOk,
     `eventsConvert "${label}": wasm ok=${w.ok}/${w.error_name}, expected ok=${wantOk}`);
  /* And the invariant that makes it matter: a successful convert must actually
     deliver the value, never report success over a dropped one. */
  const hasData = (w.events || []).some(e => e.ev === 'data');
  ok(w.ok === hasData,
     `eventsConvert "${label}": ok=${w.ok} but data event present=${hasData}`);
}

/* The resync surfaces' STATUS, on documents whose every error the reader
   recovers from and which then reach EOF cleanly — the case that clears the
   reader's final code. `ok` was already computed from an error count on all
   three, but only events() also reported the first recovered code, so parse()
   — the export index.mjs tells callers to prefer, and the one the playground
   shim uses — answered {"ok":false,"error":0,"error_name":"none"}: a caller
   doing `if (!r.ok) show(r.error_name)` was told "none" went wrong, and the
   two exports disagreed about the same document in the same module.

   The reference is the CLI: `validate` stops at the FIRST error, which is the
   one these surfaces are expected to name. */
console.log('# recovered-error status (events/parse/errors vs the CLI)');
const resyncCases = [
  ['bad byte',      '.a = <sint:32> ?;\n.b = <sint:32> 7;\n'],
  ['unit mismatch', '.a = <float:64,m/s> 1.0 k~m;\n.b = <sint:32> 7;\n'],
  ['out of range',  '.a = <uint:8> 999;\n.b = <sint:32> 7;\n'],
  ['unit illegal',  '.a = <float:64,m> 1.0;\n.b = <float:64,zz> 2.0;\n.c = <sint:32> 7;\n'],
];
for (const [label, text] of resyncCases) {
  const c = cliValidate(text);
  const e = b.events(text);
  const p = typeof b.parse === 'function' ? b.parse(text) : null;
  const r = b.errors(text);
  // The premise: the CLI rejects it, and the reader really did recover (the
  // document keeps producing errors past the first only if it resynced).
  ok(!c.ok, `resync "${label}": premise — CLI must reject it`);
  ok(r.errors.length >= 1 && !r.ok,
     `resync "${label}": errors() ok=${r.ok} n=${r.errors.length}`);
  // Every surface reports failure, and names the same error the CLI names.
  ok(!e.ok && e.error_name === c.error_name,
     `resync "${label}": events=${e.ok}/${e.error_name} cli=${c.ok}/${c.error_name}`);
  if (p) {
    ok(!p.ok && p.error_name === c.error_name,
       `resync "${label}": parse=${p.ok}/${p.error_name} cli=${c.ok}/${c.error_name}`);
    // The specific shape that regressed: a false `ok` beside a "none" code.
    ok(!(p.ok === false && p.error_name === 'none'),
       `resync "${label}": parse reported ok=false with error_name="none"`);
    // parse() is documented as the union of events() and errors(); hold it to
    // that on the status fields, not just on the two arrays.
    ok(p.error_name === e.error_name && p.errors.length === r.errors.length,
       `resync "${label}": parse=${p.error_name}/${p.errors.length} vs ` +
       `events=${e.error_name} errors=${r.errors.length}`);
  }
}

// --- the unit's own spelling (spec 1.2) --------------------------------------
// events() reports "unit" in canonical form, which is where an annotation and a
// profile's spelling are lost. "unit_source" carries what the document wrote,
// and is absent where there is no unit to have spelled.
{
  const e = b.events('#!bovnar 1.2\n.a = <float:64,ucum:mL{total}> 1.0;\n.b = 2 km;\n.c = 3;\n');
  const data = (e.events || []).filter(x => x.ev === 'data');
  ok(e.ok && data.length === 3, `unit_source: events ok=${e.ok} n=${data.length}`);
  ok(data[0] && data[0].unit_source === 'ucum:mL{total}' && data[0].unit === 'm~L',
     `unit_source: annotated ${JSON.stringify(data[0])}`);
  ok(data[1] && data[1].unit_source === 'km' && data[1].unit === 'k~m',
     `unit_source: inline ${JSON.stringify(data[1])}`);
  ok(data[2] && !('unit_source' in data[2]),
     `unit_source: unitless ${JSON.stringify(data[2])}`);
}

// --- the document tier (validate/errors vs the CLI) ---------------------------
// `bovnar validate` runs two passes: the reader, then the whole-document rules
// (array homogeneity, one shape per array of records, unique keys). validate()
// and errors() ran the first only, so each of these came back {"ok":true} from
// the module -- while toJSON() and doc() in the same module refused them, and
// the HARD validate assertion above never noticed because its corpus had no
// document-tier case in it.
console.log('# document tier (validate/errors/toJSON/doc vs the CLI)');
const docTierCases = [
  ['duplicate top-level key', '.a = 1;\n.a = 2;\n'],
  ['duplicate key in struct', '.s = {.a = 1; .a = 2;};\n'],
  ['heterogeneous array',     '.a = [1, "x"];\n'],
  ['unit mismatch in array',  '.a = [<float:64,m> 1.0, <float:64,s> 2.0];\n'],
  ['ragged matrix',           '.a = [[1, 2], [3]];\n'],
  ['record shapes differ',    '.a = [{.x = 1;}, {.y = 2;}];\n'],
  ['mismatch behind a hole',  '.a = [[null], [1], ["x"]];\n'],
];
for (const [label, text] of docTierCases) {
  const c = cliValidate(text);
  const w = b.validate(text);
  const r = b.errors(text);
  const j = b.toJSON(text);
  const d = b.doc(text);
  ok(!c.ok, `doc-tier "${label}": premise — CLI must reject it (${c.error_name})`);
  ok(!w.ok && w.error_name === c.error_name && w.tier === 'document' && w.line === 0,
     `doc-tier "${label}": validate=${w.ok}/${w.error_name}/${w.tier} cli=${c.error_name}`);
  ok(!r.ok && r.errors.length === 1 && r.errors[0].error_name === c.error_name &&
     r.errors[0].tier === 'document',
     `doc-tier "${label}": errors=${JSON.stringify(r)}`);
  ok(!j.ok && j.error_name === c.error_name, `doc-tier "${label}": toJSON=${j.error_name}`);
  // doc() used to say only {ok:false}; it names the error now.
  ok(!d.ok && d.error_name === c.error_name && d.error === w.error,
     `doc-tier "${label}": doc=${d.ok}/${d.error_name}`);
  // events() and parse() are the STREAMING view and say so in the README: the
  // reader delivered every value, so they are ok. Pinned so that a change to
  // either is a decision and not an accident.
  ok(b.events(text).ok === true, `doc-tier "${label}": events is streaming-tier`);
  if (typeof b.parse === 'function')
    ok(b.parse(text).ok === true && b.parse(text).errors.length === 0,
       `doc-tier "${label}": parse is streaming-tier`);
}
// A valid document, and a stream error, are unchanged by the second pass.
{
  const w = b.validate('.a = [1, 2];\n.s = {.x = 1;};\n');
  ok(w.ok && w.tier === null, `doc-tier: valid document ${JSON.stringify(w)}`);
  const s = b.validate('.a = @;\n');
  ok(!s.ok && s.tier === 'stream' && s.line === 1, `doc-tier: stream error ${JSON.stringify(s)}`);
  const r = b.errors('.a = @;\n.b = <uint:8> 999;\n');
  ok(r.errors.length === 2 && r.errors.every(x => x.tier === 'stream'),
     `doc-tier: stream errors keep their tier ${JSON.stringify(r)}`);
}

// --- the FIRST error, also when the last one is fatal -------------------------
// events() and parse() named the first error only when the reader's final code
// had been cleared by a clean EOF. An unterminated string holding a bad byte
// ends on incomplete_bvnr_stream, and that is what they reported, while
// errors()[0] and validate() said invalid_utf8_byte. (The string has no closing
// quote: '"\xff\xfe";' used to end that way only because recovery lost track of
// the quote, and is one error now.)
{
  const bytes = new Uint8Array([0x2e, 0x61, 0x3d, 0x22, 0xff, 0xfe]);
  const v = b.validate(bytes), r = b.errors(bytes), e = b.events(bytes);
  ok(r.errors.length >= 2, `first error: premise — several errors (${r.errors.length})`);
  ok(e.error_name === v.error_name && e.error_name === r.errors[0].error_name,
     `first error: events=${e.error_name} validate=${v.error_name} errors[0]=${r.errors[0].error_name}`);
  if (typeof b.parse === 'function') {
    const p = b.parse(bytes);
    ok(p.error_name === v.error_name && p.error === p.errors[0].error,
       `first error: parse=${p.error_name} validate=${v.error_name}`);
  }
}

// --- toJSON does not truncate a datetime --------------------------------------
// Half past midnight projected to midnight, ok:true. `bovnar convert` refuses
// the document; so does toJSON now, and says where.
{
  const text = '#!bovnar 1.1\n.when = <datetime:64> 2026-06-15T12:00:00.5Z;\n';
  const j = b.toJSON(text);
  ok(cliJson(text, { lossy: true }) === undefined, 'datetime fraction: premise — CLI refuses');
  ok(!j.ok && j.value === null && /sub-second/.test(j.detail || '') && /'when'/.test(j.detail),
     `datetime fraction: toJSON=${JSON.stringify(j)}`);
  ok(b.validate(text).ok, 'datetime fraction: the document itself is valid');
  const frac = (b.events(text).events || []).find(x => x.ev === 'data');
  ok(frac && frac.frac === '5', `datetime fraction: events keeps it ${JSON.stringify(frac)}`);
  const whole = b.toJSON('#!bovnar 1.1\n.when = <datetime:64> 2026-06-15T12:00:00Z;\n');
  ok(whole.ok && whole.value.when === 1781524800, `datetime: whole second ${JSON.stringify(whole)}`);
}

// --- wide integers are strings, as the README now says -------------------------
{
  const j = b.toJSON('.w = <uint:128> 340282366920938463463374607431768211455;\n.n = 18446744073709551615;\n');
  ok(j.ok && j.value.w === '340282366920938463463374607431768211455' &&
     typeof j.value.n === 'number', `wide integer: ${JSON.stringify(j)}`);
}

// --- a NUL does not end the unit argument early --------------------------------
// The unit crosses as a C string, so 'm\0junk' was read as 'm' and converted.
{
  const e = b.eventsConvert('.v = 1 k~m;\n', 'm\0junk');
  ok(!e.ok && e.error_name === 'unit_illegal' && e.events.length === 0,
     `NUL in unit: ${JSON.stringify(e).slice(0, 160)}`);
  const good = b.eventsConvert('.v = 1 k~m;\n', 'm');
  ok(good.ok && good.events.some(x => x.converted === '1000'), 'NUL in unit: plain unit still converts');
}

// --- reader options ----------------------------------------------------------
// bvnr_read_flags_t's assertions and limits, which had no way in from JS. Where
// the CLI has the same switch, it is the reference; the limits have no CLI
// equivalent and are pinned to the error the library documents.
console.log('# reader options');
{
  const oct = Buffer.from('.a = \x00\x01\x02\x00hi\x00;\n', 'latin1');
  ok(b.validate(oct).ok, 'options: premise — the octet document is valid');
  const w = b.validate(oct, { textOnly: true });
  const c = cliValidate(oct, ['--text-only']);
  ok(!w.ok && w.error_name === c.error_name && !c.ok,
     `options textOnly: wasm=${w.error_name} cli=${c.error_name}`);
  ok(b.events(oct, { textOnly: true }).error_name === c.error_name,
     'options textOnly: events agrees');
  ok(b.parse(oct, { textOnly: true }).error_name === c.error_name,
     'options textOnly: parse agrees');

  // A document declaring a spec version this build does not know is ACCEPTED by
  // default (the CLI does the same) and refused under strictVersion.
  const fut = '#!bovnar 9.9\n.a = 1;';
  const plain = b.validate(fut);
  ok(plain.ok && plain.declared_version === '9.9',
     `options strictVersion: accepted by default (${plain.declared_version})`);
  const s = b.validate(fut, { strictVersion: true });
  ok(!s.ok && s.error_name === 'unsupported_spec_version' &&
     s.declared_version === '9.9',
     `options strictVersion: ${JSON.stringify(s).slice(0, 120)}`);
  ok(b.parse(fut, { strictVersion: true }).error_name === s.error_name,
     'options strictVersion: parse agrees');

  ok(b.validate('.averylongkey = 1;', { maxIdentifierLength: 4 }).error_name
     === 'identifier_too_long', 'options maxIdentifierLength');
  ok(b.validate('.a = 1;', { maxFileSize: 2 }).error_name === 'file_too_long',
     'options maxFileSize');
  ok(b.validate('.a = [1,2];', { maxArrayItems: 2 }).ok,
     'options maxArrayItems: exact at the boundary');
  ok(b.validate('.a = [1,2,3];', { maxArrayItems: 2 }).error_name
     === 'too_many_array_items', 'options maxArrayItems: one over');
  ok(b.events('.a = 1;', { maxNumberLength: 1 }).ok,
     'options maxNumberLength: exact at the boundary');
  ok(!b.events('.a = 12345;', { maxNumberLength: 3 }).ok,
     'options maxNumberLength: one over');
  ok(b.validate(`.s = "${'x'.repeat(20)}";`, { maxStringLength: 5 }).error_name
     === 'string_too_long', 'options maxStringLength');
  ok(b.validate(`.s = ${'x'.repeat(20)};`, { maxSymbolLength: 5 }).error_name
     === 'symbol_too_long', 'options maxSymbolLength');
  ok(b.validate(`.r = &.${'x'.repeat(20)};`, { maxReferenceLength: 5 }).error_name
     === 'reference_too_long', 'options maxReferenceLength');
  // 64-bit limits accept a BigInt, and only then is a value past 2^53 exact.
  ok(b.validate('.a = 1;', { maxFileSize: 2n ** 40n }).ok,
     'options: a BigInt limit is accepted');
  // Nesting defaults to the format's hard limit (255) on every WASM surface.
  const deep = `${'.a = '.repeat(1)}${'['.repeat(65)}${']'.repeat(65)};`;
  ok(b.validate(deep).ok, 'options: 65 nested arrays parse at the default');

  // An unknown option is a typo, not something to ignore — the same reason a
  // read flag cannot be passed to a DOM surface and be quietly dropped.
  ok(throws(() => b.validate('.a=1;', { strictVersions: true })),
     'options: an unknown option throws');
  ok(throws(() => b.toJSON('.a=1;', { textOnly: true })),
     'options: toJSON refuses a read flag it cannot honour');
  ok(throws(() => b.doc('.a=1;', { maxFileSize: 10 })),
     'options: doc refuses a read flag it cannot honour');
}

// --- first-error position and what resync cost -------------------------------
// events() had no position at all while validate() and errors()[0] did, and no
// surface said how much of the document recovery had skipped.
console.log('# the first error position and what resync cost');
for (const [label, text] of [
  ['bad byte', '.a = @;\n.b = 1;\n'],
  ['range', '.a = <uint:8> 999;\n.b = 2;\n'],
]) {
  const v = b.validate(text);
  const e = b.events(text);
  const p = typeof b.parse === 'function' ? b.parse(text) : null;
  const r = b.errors(text);
  ok(!v.ok && e.error_name === v.error_name, `position ${label}: premise`);
  ok(e.line === v.line && e.column === v.column && e.offset === v.offset,
     `position ${label}: events=${e.line}:${e.column}@${e.offset} ` +
     `validate=${v.line}:${v.column}@${v.offset}`);
  if (p)
    ok(p.line === v.line && p.column === v.column && p.offset === v.offset,
       `position ${label}: parse=${p.line}:${p.column}@${p.offset}`);
  ok(r.error_name === v.error_name && r.line === v.line && r.offset === v.offset,
     `position ${label}: errors names and places the first error`);
  ok(e.recovery_count === r.recovery_count &&
     e.skipped_bytes === r.skipped_bytes,
     `position ${label}: events and errors agree on the recovery cost`);
  ok(e.recovery_count >= 1,
     `position ${label}: the reader recovered (${e.recovery_count})`);
}
// A clean document has no recovery and no position.
{
  const e = b.events('.a = 1;\n.b = 2;\n');
  ok(e.ok && e.recovery_count === 0 && e.skipped_bytes === 0 &&
     e.line === 0 && e.offset === 0, 'position clean: zeros');
  const r = b.errors('.a = 1;\n');
  ok(r.ok && r.error_name === 'none' && r.error === 0, 'errors clean: none');
}

// --- a float_fix's q is q, not a base ---------------------------------------
console.log('# a float_fix q parameter');
{
  const e = b.events('.v = <float_fix:16,q8> 1.5;');
  const q = (e.events || []).find(x => x.tok === 'type_q');
  ok(q && q.q === 8 && !('base' in q) && !('family' in q),
     `q param: ${JSON.stringify(q)}`);
  const data = (e.events || []).find(x => x.ev === 'data');
  ok(data && data.family === 'float_fix' && data.base === 8,
     `q data: ${JSON.stringify(data)}`);
}

// --- an empty string's text is "", not absent -------------------------------
// The event for `.s = ""` had no `text` at all, so it read like a token that
// carries none (a null literal) and `e.text.length` threw.
console.log('# an empty string carries an empty text');
{
  const e = b.events('.s = "";\n.n = null;').events.filter(x => x.ev === 'data');
  ok(e.length === 2 && e[0].tok === 'string' && e[0].text === '',
     `empty string: ${JSON.stringify(e[0])}`);
  ok(e[1].tok === 'null' && !('text' in e[1]),
     `null carries no text: ${JSON.stringify(e[1])}`);
}

// --- the exact rational behind a non-terminating conversion ------------------
// "converted": null carried no value at all: the rational is the only carrier
// in that mode, and it was unreachable (the Python binding had already had this
// exact dead end fixed).
console.log('# converted_num / converted_den');
{
  const e = b.eventsConvert('.v = <float:64,k~m/h> 100.0;', 'm/s', 10, true);
  const d = (e.events || []).find(x => x.ev === 'data');
  ok(d && d.converted === null, `rational: converted null (${JSON.stringify(d)})`);
  ok(d && d.converted_num === '250' && d.converted_den === '9',
     `rational: exact 250/9, got ${d && d.converted_num}/${d && d.converted_den}`);
  // 100 km/h = 250/9 m/s; check the decimal text of a terminating conversion
  // is unchanged and carries no rational fields.
  const t = b.eventsConvert('.v = <float:64,km> 1.5;', 'm', 10, false);
  const td = (t.events || []).find(x => x.ev === 'data');
  ok(t.ok && td && td.converted === '1500' && !('converted_num' in td),
     `rational: a terminating conversion is its text (${JSON.stringify(td)})`);
  // A base the library cannot write is refused before the document is read —
  // including when the document has no numeric value to reject it on.
  for (const bad of [1, 63, 86, 1000]) {
    const z = b.eventsConvert('.a = "text only";', '', bad);
    ok(!z.ok && z.error_name === 'invalid_argument' && z.events.length === 0,
       `rational: base ${bad} refused up front`);
  }
}

// --- the unit policy --------------------------------------------------------
// bvnr_reader_set_unit_policy / bvn_dom_parse_policy, which had no JS path at
// all. The CLI's --require-unit / --field / --require-dimension are the
// reference for the reader half; `bovnar query --unit` for the DOM half.
console.log('# unit policy (vs the CLI)');
{
  const doc = '.a = 1;\n.b = 2 m;\n';
  const req = b.createPolicy().requireUnit();
  const w = b.validate(doc, { policy: req });
  const c = cliValidate(doc, ['--require-unit']);
  ok(!w.ok && w.error_name === c.error_name,
     `policy requireUnit: wasm=${w.error_name} cli=${c.error_name}`);
  ok(!b.events(doc, { policy: req }).ok,
     'policy requireUnit: the event reader rejects it too');
  req.dispose();

  const tgt = b.createPolicy().target('m');
  const j = b.toJSON('.v = <float:64,km> 1.5;', { policy: tgt });
  const q = cliQuery('.v = <float:64,km> 1.5;', '.v', ['--unit', 'm']);
  ok(j.ok && q.ok && String(j.value.v) === q.out,
     `policy target: toJSON=${JSON.stringify(j.value)} cli=${q.out}`);
  const ev = b.events('.v = <float:64,km> 1.5;', { policy: tgt });
  const ed = (ev.events || []).find(x => x.ev === 'data');
  ok(ed && ed.converted === '1500' && ed.converted_unit === 'm',
     `policy target: the event carries the conversion (${JSON.stringify(ed)})`);
  tgt.dispose();

  // A rule names one field, asserts it, and converts it. A rule that matches
  // NOTHING is satisfied — the C contract, and the opposite of a value it
  // cannot check.
  const rule = b.createPolicy().rule('.speed', 'm/s');
  const bad = '.speed = 2 kg;';
  ok(b.validate(bad, { policy: rule }).error_name ===
     cliValidate(bad, ['--require-field', '.speed=m/s']).error_name,
     'policy rule: a wrong unit is refused, as the CLI refuses it');
  ok(b.validate('.speed = 2 m/s;', { policy: rule }).ok,
     'policy rule: a convertible value passes');
  ok(b.validate('.other = 1;', { policy: rule }).ok,
     'policy rule: a rule that matches nothing is satisfied');
  rule.dispose();

  const dim = b.createPolicy().requireConvertibleTo('m');
  ok(b.validate('.a = 1;', { policy: dim }).error_name ===
     cliValidate('.a = 1;', ['--require-dimension', 'm']).error_name,
     'policy requireConvertibleTo: a bare number is refused, as by the CLI');
  ok(b.validate('.a = 2 ft;', { policy: dim }).ok,
     'policy requireConvertibleTo: a length in another unit passes');
  dim.dispose();

  // SI normalisation converts what has a coherent SI form and leaves the rest
  // (a currency has none) alone.
  const si = b.createPolicy().normaliseSI();
  const nev = b.events('.a = 1 k~m;\n.b = 2 $USD;', { policy: si });
  const ds = (nev.events || []).filter(x => x.ev === 'data');
  ok(nev.ok && ds.length === 2 && ds[0].converted === '1000' &&
     ds[1].converted === undefined,
     `policy normaliseSI: ${JSON.stringify(ds)}`);
  si.dispose();

  // The builder refuses what the library refuses, at the call that adds it.
  const p = b.createPolicy();
  ok(throws(() => p.target('not a unit')), 'policy: an unparseable target throws');
  ok(throws(() => p.requireConvertibleTo('zzz:x')), 'policy: an unknown profile throws');
  ok(throws(() => p.rule('no-dot', 'm')), 'policy: a bad path throws');
  ok(throws(() => p.rule('.a', 'm', { mode: 'nonsense' })),
     'policy: a bad mode throws');
  ok(throws(() => p.target('m', 1)), 'policy: an unwritable base throws');
  ok(throws(() => p.target('m', 63)), 'policy: base 63 throws');
  ok(p.target('m') === p, 'policy: methods chain');
  p.dispose();
  ok(p.disposed === true && throws(() => p.target('m')),
     'policy: a disposed policy refuses to be used');
  ok(throws(() => b.validate('.a = 1;', { policy: p })),
     'policy: a disposed policy is refused by a surface');
  // Disposal is the first thing a mutator checks, not the last: a bad argument
  // on a dead handle must not read as a bad argument.
  let disposedMsg = '';
  try { p.target('m', 63); } catch (e) { disposedMsg = e.message; }
  ok(/disposed/.test(disposedMsg),
     `policy: disposal is reported before argument checks (${disposedMsg})`);

  // Count limits are the library's (8 of each); the JS builder names the limit
  // rather than passing the C side's catch-all refusal through.
  const many = b.createPolicy();
  for (let i = 0; i < 8; i++) many.target('m');
  let countMsg = '';
  try { many.target('m'); } catch (e) { countMsg = e.message; }
  ok(/at most 8 targets/.test(countMsg),
     `policy: the 9th target names the limit (${countMsg})`);
  many.dispose();
  const rules = b.createPolicy();
  for (let i = 0; i < 8; i++) rules.rule(`.k${i}`, 'm');
  ok(throws(() => rules.rule('.k8', 'm')), 'policy: the 9th rule is refused');
  rules.dispose();
  const dims = b.createPolicy();
  for (let i = 0; i < 8; i++) dims.requireConvertibleTo('m');
  ok(throws(() => dims.requireConvertibleTo('m')),
     'policy: the 9th requireConvertibleTo is refused');
  dims.dispose();

  // A policy handle belongs to the module that made it: another
  // loadBovnar() is a different WebAssembly memory, and passing the handle to
  // it must throw rather than read whatever sits at that address.
  {
    const other = await loadBovnar();
    const foreign = other.createPolicy().requireUnit();
    ok(throws(() => b.validate('.a = 1;', { policy: foreign })),
       'policy: a handle from another module instance throws');
    foreign.dispose();
    // The brand is a module-private Symbol, so an object that merely imitates
    // the old shape cannot smuggle an arbitrary pointer into the core.
    ok(throws(() => b.validate('.a = 1;', {
      policy: { _handle: () => 5, _module: b.Module },
    })), 'policy: a forged handle throws');
  }

  // Unsetting works: requireUnit(false) and normaliseSI(false) put the policy
  // back, so the same document passes again.
  {
    const toggle = b.createPolicy().requireUnit().normaliseSI();
    ok(!b.validate('.a = 1;', { policy: toggle }).ok, 'policy: toggles on');
    toggle.requireUnit(false).normaliseSI(false);
    ok(b.validate('.a = 1;', { policy: toggle }).ok, 'policy: toggles off');
    toggle.dispose();
  }

  // A converting policy and the DOM's second pass: the DOM stores converted
  // values, so homogeneity is checked over them — the same verdict
  // `bovnar query --unit` gives, and the opposite of `validate`'s as-written
  // one. Pinned so neither side drifts silently.
  {
    const text = '.a = [<float:64,km> 1.0, <float:64,m> 2.0];';
    const conv = b.createPolicy().target('m');
    ok(b.validate(text).error_name === 'array_element_type_mismatch',
       'policy DOM tier: validate judges the document as written');
    ok(cliValidate(text).error_name === 'array_element_type_mismatch',
       'policy DOM tier: the CLI validate agrees');
    ok(b.validate(text, { policy: conv }).error_name
       === 'array_element_type_mismatch',
       'policy DOM tier: validate still judges it as written under a policy');
    const d = b.doc(text, { policy: conv });
    ok(d.ok, `policy DOM tier: doc accepts the converted tree (${d.error_name})`);
    const els = d.tree[0].value.rows[0].elements;
    ok(els.length === 2 && els.every(e => e.unit === 'm'),
       `policy DOM tier: both elements are metres (${JSON.stringify(els.map(e => e.unit))})`);
    const q = cliQuery(text, '.a', ['--unit', 'm']);
    ok(q.ok, `policy DOM tier: query --unit accepts it (${q.out})`);
    conv.dispose();
  }
}

// --- version, error-name and unit helpers -----------------------------------
console.log('# version / error / unit helpers');
{
  const [maj, min, pat] = b.version().split('.').map(Number);
  ok(b.versionNumber() === maj * 10000 + min * 100 + pat,
     `versionNumber: ${b.versionNumber()} vs ${b.version()}`);
  const sv = b.specVersion();
  ok(sv.major === 1 && sv.minor === 2, `specVersion: ${JSON.stringify(sv)}`);
  const pv = b.peekVersion('#!bovnar 1.2\n.a = 1;');
  ok(pv && pv.major === 1 && pv.minor === 2, `peekVersion: ${JSON.stringify(pv)}`);
  ok(b.peekVersion('.a = 1;') === null, 'peekVersion: no directive is null');
  ok(b.peekVersion('#!bovnar 9\n.a = 1;') === null,
     'peekVersion: a malformed directive is null');
  // Every value a directive can declare must survive the crossing: 65535.65535
  // packs to the bits an in-band "absent" sentinel would have used, which is
  // why the result carries an out-parameter instead.
  const big = b.peekVersion('#!bovnar 65535.65535\n.a = 1;');
  ok(big && big.major === 65535 && big.minor === 65535,
     `peekVersion: 65535.65535 survives (${JSON.stringify(big)})`);

  ok(b.errorName(0) === 'none' && b.errorName(38) === 'unit_mismatch',
     'errorName: codes map to the names in results');

  const u = b.parseUnit('k~m/s');
  ok(u.ok && u.unit === 'k~m/s' && u.profile_only === false,
     `parseUnit: ${JSON.stringify(u)}`);
  const prof = b.parseUnit('ucum:mm[Hg]');
  ok(prof.ok && prof.unit === 'mmHg', `parseUnit profile: ${JSON.stringify(prof)}`);
  const opaque = b.parseUnit('ucum:[IU]');
  ok(opaque.ok && opaque.profile_only === true, `parseUnit opaque: ${JSON.stringify(opaque)}`);
  ok(b.parseUnit('zzz:x').error_name === 'unit_profile_unknown',
     'parseUnit: an unknown profile says so');
  ok(b.parseUnit('1').error_name === 'unit_illegal', 'parseUnit: illegal input');
  ok(b.unitsEqual('m/s', 'm·s⁻¹'), 'unitsEqual: canonical spellings agree');
  ok(!b.unitsEqual('m', 's'), 'unitsEqual: different units differ');
  ok(throws(() => b.unitsEqual('m', 'nope')), 'unitsEqual: a non-unit throws');
  ok(b.unitsConvertible('m', 'ft'), 'unitsConvertible: lengths are convertible');
  ok(!b.unitsConvertible('m', 's'), 'unitsConvertible: lengths and times are not');
  ok(throws(() => b.unitsConvertible('m', 'nope')),
     'unitsConvertible: a non-unit throws');
  const up = b.unitToProfile('k~m/s', 'ucum');
  ok(up.ok && up.unit === 'km.s-1',
     `unitToProfile: the UCUM code (${JSON.stringify(up)})`);
  ok(b.unitToProfile('k~m/s').unit === 'km.s-1',
     'unitToProfile: ucum is the default namespace');
  ok(b.unitToProfile('$USD', 'ucum').unit === null,
     'unitToProfile: a unit with no code in the vocabulary is null, not an error');
  ok(b.unitToProfile('not a unit').ok === false,
     'unitToProfile: an unparseable unit is a refusal');
  const c = b.convertUnit(0, '°C', 'K');
  ok(c.ok && Math.abs(c.value - 273.15) < 1e-9,
     `convertUnit: 0 °C is 273.15 K (${JSON.stringify(c)})`);
  const m = b.convertUnit(1.5, 'km', 'm');
  ok(m.ok && m.value === 1500, `convertUnit: 1.5 km is 1500 m (${JSON.stringify(m)})`);
  const bad = b.convertUnit(1, 'm', 's');
  ok(!bad.ok && bad.error_name === 'unit_mismatch' && bad.value === null,
     `convertUnit: incompatible units (${JSON.stringify(bad)})`);
  const fp = b.unitProfiles();
  ok(fp.ok && fp.profiles.includes('ucum') && fp.profiles.includes('udunits'),
     `unitProfiles: ${JSON.stringify(fp)}`);

  // The DOM surfaces report the version directive too (peeked, not parsed).
  ok(b.toJSON('#!bovnar 1.2\n.a = 1;').declared_version === '1.2',
     'declared_version: toJSON');
  ok(b.doc('#!bovnar 1.2\n.a = 1;').declared_version === '1.2',
     'declared_version: doc');
  ok(b.toJSON('.a = 1;').declared_version === null &&
     b.doc('.a = 1;').declared_version === null,
     'declared_version: null without a directive');
}

// --- the document tree keeps the spelling and the fraction -------------------
console.log('# doc(): unitSource and a datetime fraction');
{
  const d = b.doc('#!bovnar 1.2\n.a = <float:64,ucum:mL{total}> 1.0;\n.b = 2 km;\n.c = 3 m;');
  const [a, bb, cc] = d.tree.map(x => x.value);
  ok(a.unit === 'm~L' && a.unitSource === 'ucum:mL{total}',
     `doc unitSource: annotated ${JSON.stringify(a)}`);
  ok(bb.unit === 'k~m' && bb.unitSource === 'km',
     `doc unitSource: non-canonical spelling ${JSON.stringify(bb)}`);
  ok(cc.unit === 'm' && cc.unitSource === null,
     `doc unitSource: canonical spelling is null ${JSON.stringify(cc)}`);
  const f = b.doc('#!bovnar 1.1\n.t = <datetime:64> 2026-06-15T12:00:00.5Z;');
  const tv = f.tree[0].value;
  ok(tv.kind === 'datetime' && tv.frac === '5' && tv.epoch === 'unix',
     `doc frac: ${JSON.stringify(tv)}`);
  // The DOM surface takes a policy and applies it: the converted node has the
  // target unit and loses the document's spelling.
  const pol = b.createPolicy().target('m');
  const conv = b.doc('.v = <float:64,km> 1.5;', { policy: pol });
  pol.dispose();
  ok(conv.tree[0].value.unit === 'm' && conv.tree[0].value.unitSource === null,
     `doc policy: ${JSON.stringify(conv.tree[0].value)}`);
}


// --- report ----------------------------------------------------------------
console.log(`\nhard: ${pass} passed, ${fail} failed   |   soft toJSON diffs: ${soft}`);
if (fail) { console.log('\nFAILURES:'); for (const f of fails) console.log('  ✗ ' + f); process.exit(1); }
console.log('ALL HARD CHECKS PASSED ✓');
