// Per-workload browser benchmark for the C++ core against KaTeX 0.19.0.
//
// This is the report generator behind the tables in docs/benchmarks.md.  It
// renders every benchmark unit with both engines, verifies that the markup is
// byte identical, and reports the median of paired rounds for each unit.  The
// acceptance gate lives in `katex-browser-benchmark.mjs`; this script is for
// the user facing numbers.
//
// Usage: node tests/wasm/katex-browser-report.mjs [dist] [--json <file>]
import fs from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import { fileURLToPath } from 'node:url';
import { chromium } from '../../tools/reference/node_modules/playwright/index.mjs';

const root = path.resolve(fileURLToPath(new URL('../../', import.meta.url)));
const arguments_ = process.argv.slice(2);
const jsonIndex = arguments_.indexOf('--json');
const jsonPath = jsonIndex >= 0 ? path.resolve(arguments_[jsonIndex + 1]) : null;
const positional = arguments_.filter((value, index) => !value.startsWith('--') && index !== jsonIndex + 1);
const dist = path.resolve(positional[0] ?? path.join(root, 'npm/velocitex/dist'));
const katexDist = path.join(root, 'tools/reference/node_modules/katex/dist');
await fs.access(path.join(dist, 'runtime/velocitex-core.wasm'));

const workloads = JSON.parse(await fs.readFile(path.join(root, 'benchmarks/workloads.json'), 'utf8')).workloads;
const units = [];
for (const [group, workload] of Object.entries(workloads)) {
  for (const item of workload.cases) {
    const formulas = item.latex
      ? [{ tex: item.latex, display: !!item.displayMode }]
      : (item.paragraphs ?? [])
          .flat()
          .filter((part) => part.latex)
          .map((part) => ({ tex: part.latex, display: !!part.displayMode }));
    units.push({ group, id: item.id, formulas });
  }
}

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
                : 'text/plain'
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
  const report = await page.evaluate(async (units) => {
    const mismatches = [];
    for (const unit of units) {
      for (const formula of unit.formulas) {
        const options = { displayMode: formula.display, strict: 'ignore' };
        const actual = await window.vt.renderToString(formula.tex, options);
        const expected = window.katex.renderToString(formula.tex, options);
        if (actual !== expected) mismatches.push({ unit: unit.id, tex: formula.tex });
      }
    }

    const katexEngine = async (unit) => {
      for (const formula of unit.formulas)
        window.katex.renderToString(formula.tex, { displayMode: formula.display, strict: 'ignore' });
    };
    const coreEngine = async (unit) => {
      for (const formula of unit.formulas)
        await window.vt.renderToString(formula.tex, { displayMode: formula.display, strict: 'ignore' });
    };
    // A single formula of KaTeX costs about 0.2 ms and the same formula of the
    // core about 0.05 ms, while `performance.now()` resolves 100 us.  Repeating
    // the unit inside the timed region keeps the clock resolution a small part
    // of every round.
    const repeatsOf = (unit) => (unit.group === 'single' ? 20 : unit.group === 'long' ? 5 : 2);
    const measure = async (engine, unit) => {
      const repeats = repeatsOf(unit);
      const start = performance.now();
      for (let i = 0; i < repeats; i++) await engine(unit);
      return (performance.now() - start) / repeats;
    };

    const warmupRounds = 10;
    for (let round = 0; round < warmupRounds; round++) {
      for (const unit of units) {
        await measure(katexEngine, unit);
        await measure(coreEngine, unit);
      }
    }

    const rounds = 120;
    // Every round walks the whole corpus for both engines, so the overall
    // ratio compares two complete renders of the same 36 formulas and is not
    // dominated by the timer resolution of a single short unit.
    const katexTimes = units.map(() => []);
    const coreTimes = units.map(() => []);
    const roundRatios = [];
    const katexTotals = [];
    const coreTotals = [];
    for (let round = 0; round < rounds; round++) {
      let katexTotal = 0;
      let coreTotal = 0;
      for (let index = 0; index < units.length; index++) {
        const katex = await measure(katexEngine, units[index]);
        const core = await measure(coreEngine, units[index]);
        katexTimes[index].push(katex);
        coreTimes[index].push(core);
        katexTotal += katex;
        coreTotal += core;
      }
      katexTotals.push(katexTotal);
      coreTotals.push(coreTotal);
      roundRatios.push(katexTotal / coreTotal);
    }
    const median = (values) => [...values].sort((a, b) => a - b)[Math.floor(values.length / 2)];
    const results = units.map((unit, index) => {
      const ratios = katexTimes[index].map((value, round) => value / coreTimes[index][round]).sort((a, b) => a - b);
      return {
        group: unit.group,
        id: unit.id,
        formulas: unit.formulas.length,
        katexMs: median(katexTimes[index]),
        coreMs: median(coreTimes[index]),
        speedup: ratios[Math.floor(ratios.length / 2)],
      };
    });
    return {
      mismatches,
      results,
      speedup: median(roundRatios),
      katexUsPerFormula: (median(katexTotals) * 1000) / units.reduce((sum, unit) => sum + unit.formulas.length, 0),
      coreUsPerFormula: (median(coreTotals) * 1000) / units.reduce((sum, unit) => sum + unit.formulas.length, 0),
    };
  }, units);

  if (report.mismatches.length > 0)
    throw new Error(`the markup of ${report.mismatches.length} formulas differs from KaTeX`);
  if (errors.length > 0) throw new Error(errors.join('\n'));

  const groupLabel = { single: 'Single formula', long: 'Long formula', page: 'Mixed page' };
  let totalFormulas = 0;
  console.log('| Workload | Formulas | KaTeX (µs/formula) | VelociTex (µs/formula) | Speedup |');
  console.log('| --- | ---: | ---: | ---: | ---: |');
  for (const result of report.results) {
    const katexUs = (result.katexMs * 1000) / result.formulas;
    const coreUs = (result.coreMs * 1000) / result.formulas;
    totalFormulas += result.formulas;
    console.log(
      `| ${groupLabel[result.group]} \`${result.id}\` | ${result.formulas} | ${katexUs.toFixed(1)} | ${coreUs.toFixed(1)} | ${result.speedup.toFixed(2)}x |`
    );
  }
  console.log(
    `| **Overall** | **${totalFormulas}** | **${report.katexUsPerFormula.toFixed(1)}** | **${report.coreUsPerFormula.toFixed(1)}** | **${report.speedup.toFixed(2)}x** |`
  );
  const output = {
    generatedAt: new Date().toISOString(),
    environment: {
      browser: await page.evaluate(() => navigator.userAgent),
      platform: process.platform,
      node: process.version,
    },
    formulas: totalFormulas,
    katexUsPerFormula: report.katexUsPerFormula,
    coreUsPerFormula: report.coreUsPerFormula,
    speedup: report.speedup,
    units: report.results,
  };
  if (jsonPath) {
    await fs.mkdir(path.dirname(jsonPath), { recursive: true });
    await fs.writeFile(jsonPath, JSON.stringify(output, null, 2) + '\n');
    console.log(`\nReport written to ${jsonPath}`);
  }
} finally {
  await browser.close();
  await new Promise((resolve) => server.close(resolve));
}
