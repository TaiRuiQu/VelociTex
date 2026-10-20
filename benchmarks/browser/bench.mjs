// Dependency-free browser benchmark for the VelociTex WASM binding and KaTeX
// 0.19.0. It serves the frozen workloads.json, drives local Chrome through the
// DevTools protocol, and records parse and call-to-painted timings.
//
// Usage:
//   node benchmarks/browser/bench.mjs --engine velocitex --out artifacts/benchmarks/wasm
//   node benchmarks/browser/bench.mjs --engine katex --out artifacts/benchmarks/katex

import { createServer } from 'node:http';
import { spawn } from 'node:child_process';
import { mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import path from 'node:path';
import process from 'node:process';
import os from 'node:os';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = path.resolve(fileURLToPath(new URL('../..', import.meta.url)));
const args = process.argv.slice(2);
const option = (name, fallback) => {
  const i = args.indexOf(name);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : fallback;
};
const engine = option('--engine', 'velocitex');
if (!['velocitex', 'katex'].includes(engine)) throw new Error(`Unknown engine: ${engine}`);
const outDir = path.resolve(option('--out', path.join(root, `artifacts/benchmarks/${engine}`)));
const config = {
  samples: Number(option('--samples', '15')),
  batchMs: Number(option('--batch-ms', '25')),
  warmupMs: 200,
};
if (!Number.isInteger(config.samples) || config.samples < 3 || config.batchMs < 1)
  throw new Error('Invalid batch configuration.');
const katexDir = path.resolve(option('--katex-dir', path.join(root, 'artifacts/upstream/npm/package')));
const vtDist = path.resolve(option('--vt-dist', path.join(root, 'npm/velocitex/dist')));
const chromePath = option('--chrome', 'C:/Program Files/Google/Chrome/Application/chrome.exe');
const headless = !args.includes('--headful');
const pageDir = path.join(root, 'benchmarks/browser');

const workloadBytes = await readFile(path.join(root, 'benchmarks/workloads.json'));
const workloadSha256 = createHash('sha256').update(workloadBytes).digest('hex');
const katexVersion = JSON.parse(await readFile(path.join(katexDir, 'package.json'), 'utf8')).version;
if (katexVersion !== '0.19.0') throw new Error(`KaTeX ${katexVersion} is not the locked baseline.`);
if (engine === 'velocitex') {
  const index = await readFile(path.join(vtDist, 'index.mjs'), 'utf8');
  if (!index.includes('renderToString')) throw new Error('VelociTex dist is missing; run build-wasm.ps1.');
}
await mkdir(outDir, { recursive: true });

const contentTypes = {
  '.mjs': 'text/javascript',
  '.js': 'text/javascript',
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css',
  '.json': 'application/json',
  '.wasm': 'application/wasm',
  '.woff2': 'font/woff2',
  '.woff': 'font/woff',
  '.ttf': 'font/ttf',
};
const isolationHeaders = {
  'cross-origin-opener-policy': 'same-origin',
  'cross-origin-embedder-policy': 'require-corp',
  'cross-origin-resource-policy': 'same-origin',
  'cache-control': 'no-store',
};
const server = createServer(async (req, res) => {
  try {
    const url = new URL(req.url, 'http://127.0.0.1');
    if (url.pathname === '/' || url.pathname === '/index.html') {
      res.writeHead(200, { 'content-type': contentTypes['.html'], ...isolationHeaders });
      res.end(await readFile(path.join(pageDir, 'page.html')));
      return;
    }
    if (url.pathname === '/runner.js') {
      res.writeHead(200, { 'content-type': contentTypes['.js'], ...isolationHeaders });
      res.end(await readFile(path.join(pageDir, 'cpu-runner.js')));
      return;
    }
    if (url.pathname === '/workloads.json') {
      res.writeHead(200, { 'content-type': contentTypes['.json'], ...isolationHeaders });
      res.end(workloadBytes);
      return;
    }
    let file;
    if (url.pathname.startsWith('/katex/')) file = path.join(katexDir, 'dist', url.pathname.slice('/katex/'.length));
    else if (url.pathname.startsWith('/vt/')) file = path.join(vtDist, url.pathname.slice('/vt/'.length));
    else {
      res.writeHead(404);
      res.end();
      return;
    }
    const relative = path.relative(url.pathname.startsWith('/katex/') ? path.join(katexDir, 'dist') : vtDist, file);
    if (relative.startsWith('..') || path.isAbsolute(relative)) throw new Error('bad path');
    res.writeHead(200, {
      'content-type': contentTypes[path.extname(file)] ?? 'application/octet-stream',
      ...isolationHeaders,
    });
    res.end(await readFile(file));
  } catch (error) {
    res.writeHead(500);
    res.end(String(error));
  }
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const port = server.address().port;
const pageUrl = `http://127.0.0.1:${port}/?engine=${engine}`;
const profile = path.join(root, 'artifacts', 'tooling', `chrome-${engine}-${process.pid}-${Date.now()}`);
await mkdir(profile, { recursive: true });
const debugPort = 9500 + (process.pid % 400);
const chromeArgs = [
  `--remote-debugging-port=${debugPort}`,
  '--remote-debugging-address=127.0.0.1',
  '--remote-allow-origins=*',
  '--no-sandbox',
  '--disable-breakpad',
  '--disable-crash-reporter',
  `--user-data-dir=${profile}`,
  '--no-first-run',
  '--no-default-browser-check',
  '--disable-background-networking',
  '--disable-background-timer-throttling',
  '--disable-backgrounding-occluded-windows',
  '--disable-renderer-backgrounding',
  '--disable-features=CalculateNativeWinOcclusion',
  '--disable-extensions',
  '--hide-scrollbars',
  '--window-size=1280,1000',
  '--force-device-scale-factor=1',
  ...(headless ? ['--headless=new'] : []),
  pageUrl,
];
const chrome = spawn(chromePath, chromeArgs, { stdio: 'ignore', windowsHide: true });
let exited = false;
chrome.on('exit', () => {
  exited = true;
});

async function waitForTargets() {
  for (let attempt = 0; attempt < 300; attempt++) {
    try {
      const targets = await (await fetch(`http://127.0.0.1:${debugPort}/json/list`)).json();
      const target = targets.find((item) => item.type === 'page' && item.url.startsWith(`http://127.0.0.1:${port}/`));
      if (target) return target;
    } catch {
      /* Chrome is still starting. */
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error('Chrome did not expose the benchmark page.');
}

let ws;
try {
  const target = await waitForTargets();
  ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => {
    ws.onopen = resolve;
    ws.onerror = reject;
  });
  let nextId = 1;
  const pending = new Map();
  ws.onmessage = (event) => {
    const message = JSON.parse(event.data);
    if (!message.id) return;
    const entry = pending.get(message.id);
    if (!entry) return;
    pending.delete(message.id);
    if (message.error) entry.reject(new Error(JSON.stringify(message.error)));
    else entry.resolve(message.result);
  };
  const send = (method, params = {}) =>
    new Promise((resolve, reject) => {
      const id = nextId++;
      pending.set(id, { resolve, reject });
      ws.send(JSON.stringify({ id, method, params }));
    });
  const evaluate = async (expression) => {
    const result = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
    if (result.exceptionDetails)
      throw new Error(result.exceptionDetails.exception?.description ?? JSON.stringify(result.exceptionDetails));
    return result.result.value;
  };
  await send('Page.enable');
  for (let attempt = 0; attempt < 200; attempt++) {
    try {
      if (await evaluate('window.__benchReady === true')) break;
    } catch {
      /* document not ready */
    }
    if (attempt === 199) throw new Error('benchmark page did not become ready');
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  const hardware = await evaluate(`({
    userAgent: navigator.userAgent, devicePixelRatio, cores: navigator.hardwareConcurrency,
    memory: navigator.deviceMemory ?? null, crossOriginIsolated,
    timerResolution: (() => { const a = performance.now(); let b = a; for (let i = 0; i < 10000; i++) b = performance.now(); return b - a; })(),
  })`);
  const started = performance.now();
  const result = await evaluate(`window.runBenchmark(${JSON.stringify(config)})`);
  const wallMs = performance.now() - started;
  const screenshot = await send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false });
  await writeFile(path.join(outDir, `${engine}-page.png`), Buffer.from(screenshot.data, 'base64'));
  const report = {
    schemaVersion: 2,
    kind: `${engine === 'velocitex' ? 'VelociTex WASM binding' : 'KaTeX 0.19.0'} browser benchmark`,
    utc: new Date().toISOString(),
    engine,
    katexVersion,
    workloadSha256,
    headless,
    chrome: hardware,
    sourceRevision: execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim(),
    cpu: os.cpus()[0].model,
    memoryBytes: os.totalmem(),
    os: `${os.type()} ${os.release()} ${os.arch()}`,
    timedRegion:
      'Adaptive batches of full public API calls. MathML excludes DOM. DOM includes forced layout after every formula, excludes raster/GPU/compositor waits. Default options on both engines, all fallback cases retained. Batch medians are not individual-call latency percentiles.',
    result,
    wallMs,
  };
  await writeFile(path.join(outDir, `${engine}-report.json`), JSON.stringify(report, null, 2));
  console.log(
    `${engine}: ${wallMs.toFixed(0)}ms; ${Object.values(result.workloads).flat().length} cases; output=${outDir}`
  );
} finally {
  try {
    ws?.close();
  } catch {
    /* ignore */
  }
  if (!exited) chrome.kill();
  server.close();
  await new Promise((resolve) => setTimeout(resolve, 300));
  await rm(profile, { recursive: true, force: true }).catch(() => {});
}
