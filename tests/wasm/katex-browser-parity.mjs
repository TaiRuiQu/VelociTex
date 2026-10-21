// Browser integration parity for the KaTeX-compatible HTML/CSS fast path.
// Requires npm/velocitex/dist, tools/reference/node_modules/katex and a
// Playwright Chromium installation.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import { fileURLToPath } from 'node:url';
import { chromium } from '../../tools/reference/node_modules/playwright/index.mjs';

const root = path.resolve(fileURLToPath(new URL('../../', import.meta.url)));
const dist = path.resolve(process.argv[2] ?? path.join(root, 'npm/velocitex/dist'));
const katexDist = path.join(root, 'tools/reference/node_modules/katex/dist');
await fs.access(path.join(dist, 'runtime/vt-katex-html.wasm'));

const workloads = JSON.parse(await fs.readFile(path.join(root, 'benchmarks/workloads.json'), 'utf8')).workloads;
const formulas = [];
for (const group of Object.values(workloads))
  for (const item of group.cases) {
    if (item.latex) formulas.push({ tex: item.latex, display: !!item.displayMode });
    for (const paragraph of item.paragraphs ?? [])
      for (const part of paragraph) if (part.latex) formulas.push({ tex: part.latex, display: !!part.displayMode });
  }
for (const tex of [
  String.raw`\int_0^\infty e^{-x^2}\,dx=\frac{\sqrt{\pi}}{2}`,
  String.raw`\begin{pmatrix}a&b\\c&d\end{pmatrix}`,
  String.raw`\left\{\frac{x}{y}\right\}`,
  String.raw`\widehat{ABC}+\overrightarrow{xyz}`,
  String.raw`\color{red}{x}+\color{blue}{y}`,
  String.raw`\phantom{x}+\cancel{y}`,
  String.raw`\sqrt{\frac{a+b}{c+d}}`,
  String.raw`\reflectbox{abc}`,
  String.raw`\overlinesegment{AB}+\underlinesegment{CD}`,
  String.raw`\oiint_A B+\oiiint_C D`,
])
  formulas.push({ tex, display: false });

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
        `<!doctype html><meta charset="utf-8"><style>body{margin:0;padding:20px;font-size:24px}.cell{display:inline-block;margin:8px}</style><div id="stage"></div><script type="module">import * as vt from '/vt/index.mjs'; import katex from '/katex/katex.mjs'; window.vt=vt; window.katex=katex; window.ready=true;</script>`
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
const failures = [];
try {
  const page = await browser.newPage({ viewport: { width: 1100, height: 900 }, deviceScaleFactor: 2 });
  page.on('pageerror', (error) => failures.push(error.message));
  page.on('console', (message) => {
    if (message.type() === 'error') failures.push(message.text());
  });
  await page.goto(base);
  await page.waitForFunction(() => window.ready === true);
  const report = await page.evaluate(async (formulas) => {
    const mismatches = [];
    for (const formula of formulas) {
      const options = { displayMode: formula.display };
      const actual = await window.vt.renderToString(formula.tex, options);
      const expected = window.katex.renderToString(formula.tex, { ...options, strict: 'ignore' });
      if (actual !== expected) mismatches.push({ kind: 'html', tex: formula.tex });
      const actualMathml = await window.vt.renderToString(formula.tex, { ...options, output: 'mathml' });
      const expectedMathml = window.katex.renderToString(formula.tex, {
        ...options,
        output: 'mathml',
        strict: 'ignore',
      });
      if (actualMathml !== expectedMathml) mismatches.push({ kind: 'mathml', tex: formula.tex });
    }
    const macroOptions = { macros: { '\\RR': '\\mathbb{R}' } };
    if ((await window.vt.renderToString('\\RR', macroOptions)) !== window.katex.renderToString('\\RR', macroOptions))
      mismatches.push({ kind: 'macro' });
    const soft = await window.vt.renderToString('\\invalid', { throwOnError: false });
    if (soft !== window.katex.renderToString('\\invalid', { throwOnError: false }))
      mismatches.push({ kind: 'soft-error' });
    let diagnostic = false;
    try {
      await window.vt.renderToString('x^');
    } catch (error) {
      diagnostic = error instanceof window.vt.ParseError;
    }
    const target = document.createElement('div');
    document.body.append(target);
    await window.vt.render('x^2', target);
    if (target.innerHTML !== window.katex.renderToString('x^2')) mismatches.push({ kind: 'render' });
    const article = document.createElement('p');
    article.textContent = String.raw`Inline \(E=mc^2\) and \(x^2\).`;
    document.body.append(article);
    const result = await window.vt.renderAll(article);
    if (result.rendered !== 2 || article.querySelectorAll('.katex').length !== 2)
      mismatches.push({ kind: 'renderAll' });
    await document.fonts.ready;
    return {
      mismatches,
      diagnostic,
      stylesheet: [...document.querySelectorAll('link[rel=stylesheet]')].map((link) => link.href),
      fonts: [...document.fonts].filter((font) => font.family.includes('KaTeX')).length,
      mainFont: document.fonts.check('24px KaTeX_Main'),
      fontFaces: [...document.styleSheets]
        .flatMap((sheet) => {
          try {
            return [...sheet.cssRules];
          } catch {
            return [];
          }
        })
        .filter((rule) => rule.constructor.name === 'CSSFontFaceRule').length,
    };
  }, formulas);
  assert.deepEqual(report.mismatches, []);
  assert.equal(report.diagnostic, true);
  assert(report.stylesheet.some((href) => href.endsWith('/vt/runtime/katex.min.css')));
  assert.equal(report.mainFont, true);
  assert.deepEqual(failures, []);
  console.log(`KaTeX browser parity passed: ${formulas.length} formulas, KaTeX stylesheet and fonts loaded.`);
} finally {
  await browser.close();
  await new Promise((resolve) => server.close(resolve));
}
