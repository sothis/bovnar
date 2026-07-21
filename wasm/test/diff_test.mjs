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

// --- report ----------------------------------------------------------------
console.log(`\nhard: ${pass} passed, ${fail} failed   |   soft toJSON diffs: ${soft}`);
if (fail) { console.log('\nFAILURES:'); for (const f of fails) console.log('  ✗ ' + f); process.exit(1); }
console.log('ALL HARD CHECKS PASSED ✓');
