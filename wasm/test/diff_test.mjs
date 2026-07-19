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
import { readFileSync, readdirSync, writeFileSync, mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const CLI = join(ROOT, 'build', 'bovnar');
const tmp = mkdtempSync(join(tmpdir(), 'bvnr-diff-'));

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

// --- run -------------------------------------------------------------------
const b = await loadBovnar();
console.log(`bovnar-wasm ${b.version()}  vs  CLI @ ${CLI}\n`);

console.log('# valid examples');
for (const { name, text } of examples) {
  const w = b.validate(text);
  const c = cliValidate(text);
  ok(w.ok === c.ok && w.error_name === c.error_name,
     `validate ${name}: wasm=${w.ok}/${w.error_name} cli=${c.ok}/${c.error_name}`);

  // soft: JSON value agreement (only meaningful when both validate clean)
  if (w.ok && c.ok) {
    const wj = b.toJSON(text);
    const cj = cliJson(text);
    if (cj !== undefined && wj.ok) {
      if (!deepEq(wj.value, cj)) { soft++; console.log(`  ~ toJSON differs: ${name}`); }
    }
  }
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
