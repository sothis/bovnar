// Browser smoke test: the REAL page, in a REAL browser, running the REAL WASM.
//
// The static page-wiring check in playground_shim_test.mjs proves the scripts
// name each other. It cannot prove the chain actually boots, that the module
// instantiates, or that the three demos and the playground render values that
// came out of the reference parser. This does: it serves web/ over HTTP, loads
// index.html in headless Chrome with a virtual-time budget, and inspects the
// rendered DOM.
//
// What the assertions stand on:
//   - the playground's status is "ok" and its event count is non-zero. Both are
//     written only after parseFaithful() returns a real event stream; if the
//     WASM failed, the page says "parser unavailable" with 0 events.
//   - each demo's config message is its success message ("✓ orbit updated —
//     NYX-1", "✓ ACME restarted", "✓ reactor restarted"). applyConfig() shows
//     that ONLY after parseFaithful() returned the expected keys; an empty
//     parse (WASM not ready / failed) produces "no .orbit.altitude" instead.
//   - the mission telemetry panel shows an encoded + decoded frame with its
//     syntax classes, the trading gauges show Last/USD values, and the
//     industrial gauges show Temperature/°C values. All three are driven only
//     by decoded frames.
//
// External hosts are blackholed (Google Analytics etc.): the page must work
// offline, and CI must not depend on them. Chrome is optional: with none
// found, the test exits 77 (CTest SKIP_RETURN_CODE) and says so. Set
// BOVNAR_CHROME to a specific binary to require one.
//
// Run:  node wasm/test/browser_smoke.mjs      (from the repo root)

import { mkdtempSync, rmSync } from 'node:fs';
import { spawn, spawnSync } from 'node:child_process';
import { join, dirname } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const WEB = join(ROOT, 'web');

// ── Chrome discovery ────────────────────────────────────────────────────────
function usable(bin) {
  if (!bin) return false;
  const r = spawnSync(bin, ['--version'],
                      { encoding: 'utf8', timeout: 20000, stdio: ['ignore', 'pipe', 'pipe'] });
  return r.status === 0;
}
function findChrome() {
  if (process.env.BOVNAR_CHROME)
    return usable(process.env.BOVNAR_CHROME) ? process.env.BOVNAR_CHROME : null;
  const candidates = [
    'google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser',
    'chrome', 'chrome.exe',
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    '/Applications/Chromium.app/Contents/MacOS/Chromium',
  ];
  return candidates.find(usable) || null;
}
const explicit = !!process.env.BOVNAR_CHROME;
const chrome = findChrome();
if (!chrome) {
  if (explicit) {
    console.error(`BOVNAR_CHROME=${process.env.BOVNAR_CHROME} is not executable`);
    process.exit(1);
  }
  console.log('SKIP: no Chrome/Chromium found (set BOVNAR_CHROME to require one)');
  process.exit(77);
}

// ── a static server for web/, in its own process ───────────────────────────
// It cannot run here: spawnSync below blocks this event loop, so an in-process
// server could not answer even one request (Chrome would hang until the
// timeout). static_server.mjs prints "PORT <n>" when it is listening.
const serverProc = spawn(
  process.execPath, [join(ROOT, 'wasm', 'test', 'static_server.mjs'), WEB],
  { stdio: ['ignore', 'pipe', 'inherit'] });
const port = await new Promise((resolvePort, reject) => {
  let buf = '';
  const timer = setTimeout(
    () => reject(new Error('static server did not report a port')), 15000);
  serverProc.stdout.on('data', (d) => {
    buf += d;
    const m = /^PORT (\d+)\n/.exec(buf);
    if (m) { clearTimeout(timer); resolvePort(Number(m[1])); }
  });
  serverProc.on('exit', (code) =>
    reject(new Error(`static server exited ${code}`)));
});

// ── run the page ────────────────────────────────────────────────────────────
const profile = mkdtempSync(join(tmpdir(), 'bvnr-chrome-'));
let dom = '', why = '';
try {
  const args = [
    '--headless=new', '--disable-gpu', '--no-sandbox', '--disable-dev-shm-usage',
    '--no-first-run', '--no-default-browser-check', '--disable-extensions',
    '--disable-sync', '--mute-audio', '--hide-scrollbars',
    /* index.html negotiates the edition from navigator.languages and redirects
       a German-preferring browser to /de/; the assertions below are the English
       strings. Pin the locale so the test does not depend on the machine. */
    '--lang=en-US', '--accept-lang=en-US,en',
    '--user-data-dir=' + profile,
    /* offline and CI-stable: only the local server resolves */
    '--host-resolver-rules=MAP * ~NOTFOUND, EXCLUDE 127.0.0.1',
    '--virtual-time-budget=20000',
    '--dump-dom', `http://127.0.0.1:${port}/index.html`,
  ];
  /* stdin MUST be ignored: with a pipe left open, Chrome does not exit after
     --dump-dom (it hangs until the timeout, having written nothing). */
  const r = spawnSync(chrome, args, {
    encoding: 'utf8', timeout: 120000, maxBuffer: 64 << 20,
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  dom = r.stdout || '';
  if (r.status !== 0 || !dom) {
    why = (r.error && r.error.message) || (r.stderr || '').trim().split('\n').slice(-3).join(' | ')
          || `chrome exited ${r.status}`;
  }
} finally {
  serverProc.kill();
  try { rmSync(profile, { recursive: true, force: true }); } catch {}
}

let pass = 0, fail = 0;
const fails = [];
function ok(cond, msg) { if (cond) pass++; else { fail++; fails.push(msg); } }
function textOf(id) {
  const m = dom.match(new RegExp(`id="${id}"[^>]*>([\\s\\S]*?)</(?:span|div|b|pre)>`));
  return m ? m[1].replace(/<[^>]*>/g, '').replace(/\s+/g, ' ').trim() : '';
}
function windowOf(id, len = 2500) {
  const i = dom.indexOf(`id="${id}"`);
  return i < 0 ? '' : dom.slice(i, i + len);
}

ok(dom.length > 0, `the page rendered (${why})`);

const status = textOf('pg-status');
ok(status === 'ok', `playground status is "${status}", expected "ok"`);

const count = /^(\d+)\s+events?$/.exec(textOf('pg-ev-count'));
ok(count && Number(count[1]) > 0,
   `playground event count is "${textOf('pg-ev-count')}", expected a positive count`);

const missionMsg = textOf('cfgmsg');
ok(/✓/.test(missionMsg) && /NYX-1/.test(missionMsg),
   `mission config: "${missionMsg}" (expected the applied-config message)`);
const tradingMsg = textOf('fcfgmsg');
ok(/✓/.test(tradingMsg) && /ACME/.test(tradingMsg),
   `trading config: "${tradingMsg}" (expected the applied-config message)`);
const industrialMsg = textOf('icfgmsg');
ok(/✓/.test(industrialMsg) && /reactor/.test(industrialMsg),
   `industrial config: "${industrialMsg}" (expected the applied-config message)`);

const frameNo = /id="s-frame"[^>]*>(\d+)</.exec(dom);
ok(frameNo && Number(frameNo[1]) > 0,
   `mission frame counter is "${frameNo ? frameNo[1] : ''}", expected it to advance`);
const frames = windowOf('framesrc');
ok(frames.includes('#!bovnar') && frames.includes('bvh-key'),
   'mission telemetry frame is not shown with its syntax classes');
const trading = windowOf('fgauges');
ok(/Last/.test(trading) && /USD/.test(trading) && /\d+\.\d\d/.test(trading),
   'trading gauges do not show a decoded Last/USD value');
const industrial = windowOf('igauges');
ok(/Temperature/.test(industrial) && /°C/.test(industrial) && /\d/.test(industrial),
   'industrial gauges do not show a decoded Temperature/°C value');

console.log(`browser: ${chrome}`);
console.log(`${pass} passed, ${fail} failed`);
if (fail) {
  console.log('\nFAILURES:');
  for (const f of fails) console.log('  ✗ ' + f);
  process.exit(1);
}
console.log('ALL CHECKS PASSED ✓');
