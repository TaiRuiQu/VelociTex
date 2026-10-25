// Browser performance acceptance for the C++ core.
//
// The requirement is that the C++ core renders at least five times faster
// than KaTeX's JavaScript implementation, measured in a real browser against
// the shipped package.  The test renders the same formulas with both engines,
// verifies that the markup is byte identical, and then compares the
// throughput of repeated renders.
//
// Usage: node tests/wasm/katex-browser-benchmark.mjs [dist] [--min-ratio 5]
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import { fileURLToPath } from 'node:url';
import { chromium } from '../../tools/reference/node_modules/playwright/index.mjs';

const root = path.resolve(fileURLToPath(new URL('../../', import.meta.url)));
const args = process.argv.slice(2).filter((value) => !value.startsWith('--'));
const ratioIndex = process.argv.indexOf('--min-ratio');
const minimumRatio = ratioIndex >= 0 ? Number(process.argv[ratioIndex + 1]) : 5;
const dist = path.resolve(args[0] ?? path.join(root, 'npm/velocitex/dist'));
const katexDist = path.join(root, 'tools/reference/node_modules/katex/dist');
await fs.access(path.join(dist, 'runtime/velocitex-core.wasm'));

// The frozen benchmark workloads are the formula set both engines render.
const workloads = JSON.parse(await fs.readFile(path.join(root, 'benchmarks/workloads.json'), 'utf8')).workloads;
const formulas = [];
for (const group of Object.values(workloads))
  for (const item of group.cases) {
    if (item.latex) formulas.push({ tex: item.latex, display: !!item.displayMode });
    for (const paragraph of item.paragraphs ?? [])
      for (const part of paragraph) if (part.latex) formulas.push({ tex: part.latex, display: !!part.displayMode });
  }
assert(formulas.length > 0, 'the benchmark workloads are empty');

const server = http.createServer(async (request, response) => {
  try {
    const url = new URL(request.url, 'http://localhost');
    if (url.pathname === '/favicon.ico') {
      response.writeHead(204);
      response.end();
      return;
    }
    if (url.pathname === '/') {
      response.setHeader('content-type', 'text/html; charset=utf-8');
      response.end(
        `<!doctype html><meta charset="utf-8"><style>body{margin:0;padding:20px;font-size:24px}</style><div id="stage"></div><script type="module">import * as vt from '/vt/index.mjs'; import katex from '/katex/katex.mjs'; window.vt=vt; window.katex=katex; window.ready=true;</script>`
      );
      return;
    }
    let base;
    let relative;
    if (url.pathname.startsWith('/vt/')) {
      base = dist;
      relative = url.pathname.slice(4);
    } else if (url.pathname.startsWith('/katex/')) {
      base = katexDist;
      relative = url.pathname.slice(7);
    } else {
      response.writeHead(404);
      response.end();
      return;
    }
    const file = path.resolve(base, decodeURIComponent(relative));
    if (!file.startsWith(base + path.sep)) throw new Error('Path traversal');
    const data = await fs.readFile(file);
    const extension = path.extname(file);
    response.setHeader(
      'content-type',
      extension === '.mjs' || extension === '.js'
        ? 'text/javascript'
        : extension === '.wasm'
          ? 'application/wasm'
          : extension === '.css'
            ? 'text/css'
            : extension === '.woff2'
              ? 'font/woff2'
              : extension === '.woff'
                ? 'font/woff'
                : extension === '.ttf'
                  ? 'font/ttf'
                  : 'application/octet-stream'
    );
    response.end(data);
  } catch (error) {
    response.writeHead(404);
    response.end(String(error));
  }
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const base = `http://127.0.0.1:${server.address().port}`;
const browser = await chromium.launch({
  executablePath: process.env.VELOCITEX_BROWSER ?? chromium.executablePath(),
  headless: true,
  args: ['--force-color-profile=srgb', '--disable-lcd-text'],
});
try {
  const page = await browser.newPage({ viewport: { width: 1100, height: 900 } });
  const errors = [];
  page.on('pageerror', (error) => errors.push(error.message));
  await page.goto(base);
  await page.waitForFunction(() => window.ready === true);
  const report = await page.evaluate(async (formulas) => {
    const mismatches = [];
    // Correctness first: the two engines must agree byte for byte.
    for (const formula of formulas) {
      const options = { displayMode: formula.display, strict: 'ignore' };
      const actual = await window.vt.renderToString(formula.tex, options);
      const expected = window.katex.renderToString(formula.tex, options);
      if (actual !== expected) mismatches.push({ tex: formula.tex });
    }

    const measure = async (engine, rounds) => {
      // Warm the engine up so that the measurement reflects steady state.
      for (const formula of formulas) await engine(formula);
      const start = performance.now();
      for (let round = 0; round < rounds; round++) {
        for (const formula of formulas) await engine(formula);
      }
      return performance.now() - start;
    };

    const katexEngine = async (formula) =>
      window.katex.renderToString(formula.tex, { displayMode: formula.display, strict: 'ignore' });
    const coreEngine = async (formula) =>
      window.vt.renderToString(formula.tex, { displayMode: formula.display, strict: 'ignore' });

    // Warm both engines up before the measurement.  The WebAssembly module is
    // large, so the browser needs a few hundred calls before it reaches its
    // steady state; KaTeX gets the same treatment.
    const warmupRounds = 25;
    for (let round = 0; round < warmupRounds; round++) {
      await measure(katexEngine, 1);
      await measure(coreEngine, 1);
    }

    // Interleave the rounds so that a slow machine cannot favour one engine.
    // The rounds are paired: every round measures both engines one after the
    // other, so the ratio of a round compares the same machine state.
    //
    // The reported number is the median of the per-round ratios.  On a busy
    // machine the fastest sample of an engine is not a reliable estimator: two
    // runs of the same pair differed by six percent, because a round that
    // caught an unrelated system hiccup moved one engine but not the other.
    // The median of the paired ratios does not depend on such an outlier; its
    // noise floor is below one percent.
    const rounds = 120;
    const katexTimes = [];
    const coreTimes = [];
    for (let round = 0; round < rounds; round++) {
      katexTimes.push(await measure(katexEngine, 1));
      coreTimes.push(await measure(coreEngine, 1));
    }
    const ratios = katexTimes.map((katex, index) => katex / coreTimes[index]).sort((a, b) => a - b);
    const median = (values) => values[Math.floor(values.length / 2)];
    const sortedKatex = [...katexTimes].sort((a, b) => a - b);
    const sortedCore = [...coreTimes].sort((a, b) => a - b);
    return {
      mismatches,
      speedup: median(ratios),
      bestSpeedup: Math.min(...katexTimes) / Math.min(...coreTimes),
      katexTotal: Math.min(...katexTimes),
      coreTotal: Math.min(...coreTimes),
      katexMedian: median(sortedKatex),
      coreMedian: median(sortedCore),
      formulas: formulas.length,
      rounds,
    };
  }, formulas);
  assert.deepEqual(errors, []);
  assert.deepEqual(
    report.mismatches.map((mismatch) => mismatch.tex),
    [],
    `the markup of ${report.mismatches.length} formulas differs from KaTeX`
  );
  console.log(
    `browser benchmark: ${report.formulas} formulas x ${report.rounds} rounds - ` +
      `KaTeX(JS) ${report.katexTotal.toFixed(1)} ms (median ${report.katexMedian.toFixed(1)} ms), ` +
      `VelociTex(C++ WASM) ${report.coreTotal.toFixed(1)} ms (median ${report.coreMedian.toFixed(1)} ms), ` +
      `speedup ${report.speedup.toFixed(2)}x (best round ${report.bestSpeedup.toFixed(2)}x)`
  );
  const speedup = report.speedup;
  assert(
    speedup >= minimumRatio,
    `the C++ core is ${speedup.toFixed(2)}x faster than KaTeX, which is below the required ${minimumRatio}x`
  );
  console.log(`browser benchmark passed: ${speedup.toFixed(2)}x >= ${minimumRatio}x`);
} finally {
  await browser.close();
  await new Promise((resolve) => server.close(resolve));
}
