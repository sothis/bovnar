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
function cliJson(text) {
  const f = join(tmp, 'in.bvnr');
  writeFileSync(f, text);
  try { return JSON.parse(execFileSync(CLI, ['convert', f], { encoding: 'utf8' })); }
  catch { return undefined; }
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
  const c = cliJson(text);
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

// --- report ----------------------------------------------------------------
console.log(`\nhard: ${pass} passed, ${fail} failed   |   soft toJSON diffs: ${soft}`);
if (fail) { console.log('\nFAILURES:'); for (const f of fails) console.log('  ✗ ' + f); process.exit(1); }
console.log('ALL HARD CHECKS PASSED ✓');
