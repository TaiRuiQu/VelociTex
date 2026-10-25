// Diagnostic: splits the browser render time into its JavaScript overhead and
// the WebAssembly call itself.
import fs from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import { fileURLToPath } from 'node:url';
import { chromium } from '../../tools/reference/node_modules/playwright/index.mjs';

const root = path.resolve(fileURLToPath(new URL('../../', import.meta.url)));
const dist = path.join(root, 'npm/velocitex/dist');
const katexDist = path.join(root, 'tools/reference/node_modules/katex/dist');
const workloads = JSON.parse(await fs.readFile(path.join(root, 'benchmarks/workloads.json'), 'utf8')).workloads;
const formulas = [];
// The same formula set the benchmark harness renders, so the numbers are
// comparable with `katex-browser-benchmark.mjs`.
for (const group of Object.values(workloads))
  for (const item of group.cases) {
    if (item.latex) formulas.push(item.latex);
    for (const paragraph of item.paragraphs ?? [])
      for (const part of paragraph) if (part.latex) formulas.push(part.latex);
  }

const server = http.createServer(async (request, response) => {
  const url = new URL(request.url, 'http://localhost');
  if (url.pathname === '/') {
    response.setHeader('content-type', 'text/html; charset=utf-8');
    response.end(
      `<!doctype html><meta charset="utf-8"><div id="stage"></div><script type="module">import * as vt from '/vt/index.mjs'; import katex from '/katex/katex.mjs'; import {renderKatexMath} from '/vt/katexmath.mjs'; window.vt=vt; window.katex=katex; window.renderKatexMath=renderKatexMath; window.ready=true;</script>`
    );
    return;
  }
  const base = url.pathname.startsWith('/vt/') ? dist : katexDist;
  const relative = url.pathname.startsWith('/vt/') ? url.pathname.slice(4) : url.pathname.slice(7);
  try {
    const data = await fs.readFile(path.resolve(base, decodeURIComponent(relative)));
    const extension = path.extname(relative);
    response.setHeader(
      'content-type',
      extension === '.mjs' ? 'text/javascript' : extension === '.wasm' ? 'application/wasm' : 'text/plain'
    );
    response.end(data);
  } catch {
    response.writeHead(404);
    response.end();
  }
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const base = `http://127.0.0.1:${server.address().port}`;
const browser = await chromium.launch({ headless: true });
try {
  const page = await browser.newPage();
  page.on('pageerror', (error) => console.log('PAGE ERROR', error.message));
  await page.goto(base);
  await page.waitForFunction(() => window.ready === true);
  const report = await page.evaluate(async (formulas) => {
    await window.vt.init({ injectStyles: false });
    const options = JSON.stringify({ strict: 'ignore' });
    // The measurement has to run against the steady state: the first calls
    // are still interpreted, so they say nothing about the throughput.
    const time = async (fn, rounds) => {
      let best = Infinity;
      for (let r = 0; r < rounds; r++) {
        const start = performance.now();
        for (const f of formulas) await fn(f);
        best = Math.min(best, performance.now() - start);
      }
      return best;
    };
    const timeSync = (fn, rounds) => {
      let best = Infinity;
      for (let r = 0; r < rounds; r++) {
        const start = performance.now();
        for (const f of formulas) fn(f);
        best = Math.min(best, performance.now() - start);
      }
      return best;
    };
    const engines = {
      katex: async (f) => window.katex.renderToString(f, { strict: 'ignore' }),
      vt: async (f) => window.vt.renderToString(f, { strict: 'ignore' }),
      raw: async (f) => window.renderKatexMath(f, options, 0),
    };
    // Warm every engine up before the measurement, exactly like the
    // benchmark harness does.
    const warmupRounds = 25;
    for (let r = 0; r < warmupRounds; r++)
      for (const f of formulas) for (const engine of Object.values(engines)) await engine(f);
    const rounds = 7;
    const katexMs = await time(engines.katex, rounds);
    const vtMs = await time(engines.vt, rounds);
    const rawMs = await time(engines.raw, rounds);
    const rawSyncMs = timeSync((f) => window.renderKatexMath(f, options, 0), rounds);
    return {
      formulas: formulas.length,
      rounds,
      katexMs,
      vtMs,
      rawMs,
      rawSyncMs,
    };
  }, formulas);
  const calls = report.formulas * report.rounds;
  console.log(`calls per engine: ${calls}`);
  console.log(`KaTeX(JS)            : ${(report.katexMs / calls).toFixed(4)} ms/call`);
  console.log(`VelociTex renderToString: ${(report.vtMs / calls).toFixed(4)} ms/call`);
  console.log(`VelociTex raw WASM (async): ${(report.rawMs / calls).toFixed(4)} ms/call`);
  console.log(`VelociTex raw WASM (sync) : ${(report.rawSyncMs / calls).toFixed(4)} ms/call`);
} finally {
  await browser.close();
  await new Promise((resolve) => server.close(resolve));
}
