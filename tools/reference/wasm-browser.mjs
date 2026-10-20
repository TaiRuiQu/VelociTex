// Browser integration verification against a source build or an installed npm package.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';

const root = fileURLToPath(new URL('../../', import.meta.url));
const packageRoot = path.resolve(process.argv[2] ?? path.join(root, 'npm/velocitex'));
const staticRoot = path.resolve(process.argv[3] ?? path.join(packageRoot, 'dist/runtime'));
const output = path.join(root, 'artifacts/wasm/browser');
await fs.mkdir(output, { recursive: true });
const failures = [];
const requests = [];
const server = http.createServer(async (req, res) => {
  try {
    const url = new URL(req.url, 'http://localhost');
    if (url.pathname === '/') {
      res.setHeader('Content-Type', 'text/html; charset=utf-8');
      res.end('<!doctype html><meta charset="utf-8"><title>VelociTex WASM verification</title><style>body{font:18px Arial,sans-serif;padding:32px;color:#182036;line-height:1.6}h1{font-size:24px}#article{max-width:900px}code{background:#eee}#dark{background:#182036;color:#f6f6ff;padding:20px}</style><h1>VelociTex · Browser WASM</h1><main id="article"></main><div id="dark"></div>');
      return;
    }
    if (url.pathname === '/favicon.ico') { res.writeHead(204); res.end(); return; }
    const isStatic = url.pathname.startsWith('/static/');
    const relative = isStatic ? url.pathname.slice(8) : url.pathname.slice('/package/'.length);
    if (!url.pathname.startsWith('/package/') && !url.pathname.startsWith('/static/')) throw new Error('Not found');
    const directory = isStatic ? staticRoot : packageRoot;
    const file = path.resolve(directory, decodeURIComponent(relative));
    if (!file.startsWith(directory + path.sep)) throw new Error('Path traversal');
    const data = await fs.readFile(file);
    const type = { '.mjs': 'text/javascript', '.js': 'text/javascript', '.json': 'application/json', '.wasm': 'application/wasm' }[path.extname(file)] ?? 'application/octet-stream';
    res.setHeader('Content-Type', type); res.end(data);
    requests.push({ path: url.pathname, bytes: data.length });
  } catch (error) { res.writeHead(404); res.end(error.message); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const base = `http://127.0.0.1:${server.address().port}`;
const browser = await chromium.launch({ executablePath: process.env.VELOCITEX_BROWSER ?? (process.platform === 'win32' ? 'C:/Program Files/Google/Chrome/Application/chrome.exe' : chromium.executablePath()), headless: true, args: ['--force-color-profile=srgb', '--disable-lcd-text'] });
try {
  const page = await browser.newPage({ viewport: { width: 1100, height: 1000 }, deviceScaleFactor: 1 });
  page.on('pageerror', error => failures.push(error.message));
  page.on('console', message => { if (message.type() === 'error') failures.push(message.text()); });
  await page.goto(base);
  const api = await page.evaluate(async () => {
    const vt = await import('/package/dist/index.mjs');
    globalThis.vt = vt;
    const article = document.querySelector('#article');
    article.innerHTML = String.raw`<p>Inline \(E=mc^2\), fraction \(\frac{a+b}{c}\), and square root \(\sqrt{x^2+y^2}\).</p><div data-math="\int_0^1 x^2\,dx" data-display-mode="true"></div><p>Currency $20 stays literal. Escaped \\(x\\) stays literal.</p><pre>\(do not render\)</pre><p class="velocitex-ignore">\(ignored\)</p><div contenteditable="true">\(editable\)</div><p>Bad: \(\invalidcommand\).</p>`;
    const [report, concurrent] = await Promise.all([vt.renderAll(article, { fastMathml: false }), vt.renderToString('x^2', { fastMathml: false })]);
    const repeated = await vt.renderAll(article, { fastMathml: false });
    const ids = [...document.querySelectorAll('svg [id]')].map(n => n.id);
    const referencesResolve = [...document.querySelectorAll('svg use')].every(n => document.getElementById(n.getAttribute('href').slice(1)));
    let diagnostic;
    try { await vt.renderToString('x^'); } catch (error) { diagnostic = { isParseError: error instanceof vt.ParseError, code: error.code, range: error.range }; }
    const errorHtml = await vt.renderToString(String.raw`\invalid{<img src=x onerror=alert(1)>}`, { throwOnError: false });
    let invalidArgument = false;
    try { await vt.renderToString('x', { maxExpand: 1.5 }); } catch (error) { invalidArgument = error instanceof TypeError; }
    const macro = await vt.renderToString(String.raw`\RR`, { macros: { '\\RR': '\\mathbb{R}' }, output: 'mathml' });
    let macroIsolated = false;
    try { await vt.renderToString(String.raw`\RR`); } catch (error) { macroIsolated = error instanceof vt.ParseError; }
    let fallback;
    try { await vt.renderToString(String.raw`\text{中文}`, { fastMathml: false }); } catch (error) { fallback = error.code; }
    const fastMathml = await vt.renderToString(String.raw`\frac{a}{b}`, { output: 'mathml', fastMathml: true });
    const fastHtml = await vt.renderToString('x^2', { fastMathml: true });
    const fastFallback = await vt.renderToString(String.raw`\color{red}{x}`, { fastMathml: true });
    const single = document.createElement('div'); single.textContent = '$x$ and $$y$$'; article.append(single);
    const dollars = await vt.renderAll(single, { fastMathml: false, delimiters: [{ left: '$', right: '$', display: false }, { left: '$$', right: '$$', display: true }] });
    const fragment = document.createDocumentFragment();
    const fragmentText = document.createElement('div'); fragmentText.textContent = String.raw`\(z\)`; fragment.append(fragmentText);
    const fragmentReport = await vt.renderAll(fragment, { fastMathml: false });
    const rootMatch = document.createElement('div'); rootMatch.dataset.math = 'a+b';
    const rootReport = await vt.renderAll(rootMatch, { fastMathml: false });
    const nested = document.createElement('div'); nested.innerHTML = '<div data-math="x"><span data-math="y"></span></div>';
    const nestedReport = await vt.renderAll(nested, { fastMathml: false });
    const limit = document.createElement('div'); limit.textContent = String.raw`\(x\) \(y\)`;
    let bounded = false;
    try { await vt.renderAll(limit, { maxFormulas: 1 }); } catch (error) { bounded = error instanceof RangeError; }
    return { rendered: report.rendered, errors: report.errors.map(e => e.error.code), repeated: repeated.rendered,
      uniqueIds: ids.length === new Set(ids).size, referencesResolve, concurrent: concurrent.includes('<svg'), diagnostic,
      errorEscaped: !errorHtml.includes('<img') && errorHtml.includes('&lt;img'), invalidArgument, macro: macro.includes('double-struck'), macroIsolated, fallback,
      fastMathml: fastMathml.includes('<mfrac>'), fastHtml: fastHtml.includes('<math'), fastFallback: fastFallback.includes('<svg'),
      dollars: dollars.rendered, fragment: fragmentReport.rendered, rootMatch: rootReport.rendered, nested: nestedReport.rendered, bounded,
      skipped: article.querySelector('pre').textContent === String.raw`\(do not render\)` && !article.querySelector('[contenteditable] svg'),
    };
  });
  console.log(JSON.stringify(api));
  assert.equal(api.rendered, 4); assert.equal(api.errors.length, 1); assert.equal(api.repeated, 0);
  for (const key of ['uniqueIds', 'referencesResolve', 'concurrent', 'errorEscaped', 'invalidArgument', 'macro', 'macroIsolated', 'bounded', 'skipped', 'fastMathml', 'fastHtml', 'fastFallback']) assert.equal(api[key], true, key);
  assert.equal(api.diagnostic.isParseError, true); assert.equal(api.fallback, 'UnknownSymbol');
  assert.equal(api.dollars, 2); assert.equal(api.fragment, 1); assert.equal(api.rootMatch, 1); assert.equal(api.nested, 1);
  const corpusPath = path.join(root, 'artifacts/wasm/parity.json');
  let parity;
  try { parity = JSON.parse(await fs.readFile(corpusPath, 'utf8')); } catch (error) { if (error.code !== 'ENOENT') throw error; }
  let parityReport = { verified: false };
  if (parity) {
    parityReport = await page.evaluate(async fixtures => {
      const errors = [];
      let valid = 0, invalid = 0;
      for (const fixture of fixtures) {
        try {
          const svg = await vt.renderToString(fixture.source, { ...fixture.options, output: 'svg', fastMathml: false });
          const mathml = await vt.renderToString(fixture.source, { ...fixture.options, output: 'mathml', fastMathml: false });
          if (!fixture.success || svg.replace(/vt[a-z0-9]+_\d+/g, 'parity') !== fixture.svg || mathml !== fixture.mathml) errors.push(fixture.id + ': output differs');
          valid++;
        } catch (error) {
          if (fixture.success || error.code !== fixture.code || error.range?.start !== fixture.start || error.range?.length !== fixture.length) errors.push(fixture.id + ': diagnostic differs: ' + error.message);
          invalid++;
        }
      }
      return { verified: true, valid, invalid, errors };
    }, parity);
    assert.deepEqual(parityReport.errors, []);
  }
  await page.evaluate(async () => {
    const gallery = document.createElement('section'); gallery.id = 'gallery';
    const formulas = [String.raw`\int_0^\infty e^{-x^2}\,dx=\frac{\sqrt{\pi}}{2}`, String.raw`\begin{pmatrix}a&b\\c&d\end{pmatrix}`, String.raw`\left\{\frac{x}{y}\right\}`, String.raw`\widehat{ABC}+\overrightarrow{xyz}`, String.raw`\color{red}{x}+\color{blue}{y}`, String.raw`\phantom{x}+\cancel{y}`, String.raw`\sqrt{\frac{a+b}{c+d}}`];
    document.querySelector('#article').append(gallery);
    for (const formula of formulas) { const host = document.createElement('div'); host.style.padding = '10px'; gallery.append(host); await vt.render(formula, host, { fastMathml: false }); }
    await vt.render(String.raw`\frac{a}{b}+\sqrt{x}`, document.querySelector('#dark'), { fastMathml: false });
  });
  await page.screenshot({ path: path.join(output, 'browser.png'), fullPage: true });
  await page.emulateMedia({ colorScheme: 'dark' });
  await page.setViewportSize({ width: 480, height: 900 });
  await page.screenshot({ path: path.join(output, 'browser-narrow.png'), fullPage: true });
  const custom = await browser.newPage();
  await custom.goto(base);
  const relocated = await custom.evaluate(async () => {
    const { init, renderToString } = await import('/package/dist/index.mjs');
    await init({ assetBaseUrl: '/static/' });
    return (await renderToString('a+b', { fastMathml: false })).includes('<svg');
  });
  assert.equal(relocated, true);
  assert.deepEqual(failures, []);
  const report = { packageRoot, staticRoot, browser: browser.version(), api, parity: parityReport, relocated,
    runtimeRequestedBytes: requests.filter(r => r.path.includes('/runtime/')).reduce((n, r) => n + r.bytes, 0),
    requests, failures };
  await fs.writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(`WASM browser verification passed: ${parityReport.valid ?? 0} parity successes, ${parityReport.invalid ?? 0} parity diagnostics.`);
} finally { await browser.close(); await new Promise(resolve => server.close(resolve)); }
