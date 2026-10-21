// Capture the WASM-binding and KaTeX renderings used by the comparison figure.
// The Windows-native renderings are produced by VelociTex.Benchmarks
// --render-comparison; matplotlib composes all three columns.

import { createServer } from 'node:http';
import { spawn } from 'node:child_process';
import { mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const root = path.resolve(fileURLToPath(new URL('../..', import.meta.url)));
const args = process.argv.slice(2);
const option = (name, fallback) => {
  const i = args.indexOf(name);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : fallback;
};
const outDir = path.resolve(option('--out', path.join(root, 'artifacts/comparison')));
const katexDir = path.resolve(option('--katex-dir', path.join(root, 'artifacts/upstream/npm/package')));
const vtDist = path.resolve(option('--vt-dist', path.join(root, 'npm/velocitex/dist')));
const chromePath = option('--chrome', 'C:/Program Files/Google/Chrome/Application/chrome.exe');
const headless = !args.includes('--headful');
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
  '.png': 'image/png',
};
const server = createServer(async (req, res) => {
  try {
    const url = new URL(req.url, 'http://127.0.0.1');
    if (url.pathname === '/' || url.pathname === '/comparison.html') {
      res.writeHead(200, { 'content-type': contentTypes['.html'] });
      res.end(await readFile(path.join(root, 'benchmarks/browser/comparison.html')));
      return;
    }
    if (url.pathname === '/comparison.js') {
      res.writeHead(200, { 'content-type': contentTypes['.js'] });
      res.end(await readFile(path.join(root, 'benchmarks/browser/comparison.js')));
      return;
    }
    if (url.pathname === '/comparison.json') {
      res.writeHead(200, { 'content-type': contentTypes['.json'] });
      res.end(await readFile(path.join(root, 'benchmarks/comparison.json')));
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
    res.writeHead(200, {
      'content-type': contentTypes[path.extname(file)] ?? 'application/octet-stream',
      'cache-control': 'no-store',
    });
    res.end(await readFile(file));
  } catch (error) {
    res.writeHead(500);
    res.end(String(error));
  }
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const port = server.address().port;
const pageUrl = `http://127.0.0.1:${port}/comparison.html`;
const profile = path.join(root, 'artifacts', 'tooling', `chrome-comparison-${process.pid}-${Date.now()}`);
await mkdir(profile, { recursive: true });
const debugPort = 9900 + (process.pid % 90);
const chrome = spawn(
  chromePath,
  [
    `--remote-debugging-port=${debugPort}`,
    '--remote-debugging-address=127.0.0.1',
    '--remote-allow-origins=*',
    '--no-sandbox',
    '--disable-breakpad',
    '--disable-crash-reporter',
    `--user-data-dir=${profile}`,
    '--no-first-run',
    '--no-default-browser-check',
    '--disable-background-timer-throttling',
    '--disable-backgrounding-occluded-windows',
    '--hide-scrollbars',
    '--window-size=1500,2600',
    '--force-device-scale-factor=2',
    ...(headless ? ['--headless=new'] : []),
    pageUrl,
  ],
  { stdio: 'ignore', windowsHide: true }
);
let exited = false;
chrome.on('exit', () => {
  exited = true;
});
let ws;
try {
  let target;
  for (let attempt = 0; attempt < 300; attempt++) {
    try {
      const targets = await (await fetch(`http://127.0.0.1:${debugPort}/json/list`)).json();
      target = targets.find((item) => item.type === 'page' && item.url.startsWith(pageUrl));
      if (target) break;
    } catch {
      /* Chrome is still starting. */
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  if (!target) throw new Error('Chrome did not expose the comparison page.');
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
  for (let attempt = 0; attempt < 300; attempt++) {
    const pageError = await evaluate('window.__comparisonError ?? null');
    if (pageError) throw new Error(pageError);
    try {
      if (await evaluate('window.__ready === true')) break;
    } catch {
      /* not ready */
    }
    if (attempt === 299) throw new Error('comparison page did not become ready');
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await evaluate('document.fonts.ready.then(() => true)');
  await writeFile(
    path.join(outDir, 'browser-validation.json'),
    JSON.stringify(await evaluate('window.comparisonValidation'), null, 2)
  );
  const formulas = JSON.parse(await readFile(path.join(root, 'benchmarks/comparison.json'), 'utf8'));
  for (const formula of formulas) {
    for (const engine of ['wasm', 'katex']) {
      const rect = await evaluate(`window.cellRect(${JSON.stringify(`${formula.id}-${engine}`)})`);
      const screenshot = await send('Page.captureScreenshot', {
        format: 'png',
        captureBeyondViewport: true,
        clip: { x: rect.x, y: rect.y, width: rect.width, height: rect.height, scale: 1 },
      });
      await writeFile(path.join(outDir, `${formula.id}-${engine}.png`), Buffer.from(screenshot.data, 'base64'));
      console.log(`${formula.id}-${engine}: ${Math.round(rect.width)}x${Math.round(rect.height)} CSS px`);
    }
  }
  const screenshot = await send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: true });
  await writeFile(path.join(outDir, 'comparison-page.png'), Buffer.from(screenshot.data, 'base64'));
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
