import * as vt from "/vt/index.mjs";

const parameters = new URLSearchParams(location.search);
const iterations = Number(parameters.get("iterations") ?? "200");
const pageRounds = Number(parameters.get("pageRounds") ?? "5");
const corpus = await (await fetch("/corpus.json")).json();
// The portable browser runtime intentionally has no host text shaper, so
// formulas requiring system CJK outlines are diagnostics rather than renders.
// Performance comparison uses the same supported formula set for both engines.
const FIXTURES = corpus.fixtures.filter(f => ![...f.latex].some(c => c.charCodeAt(0) > 127))
  .map(f => ({ id: f.id, category: f.category, latex: f.latex, displayMode: f.displayMode }));
const COMPLEX = FIXTURES.filter(f => ["matrices", "aligned", "calculus", "text-nesting"].includes(f.category)).slice(0, 12);
const OPTIONS = { throwOnError: true, trust: false, maxExpand: 1000, maxSize: 20, strict: "ignore" };
const stage = document.getElementById("stage");

const PAGE = (() => {
  const sources = [];
  for (let i = 0; i < 100; i++) {
    if (i < FIXTURES.length) { sources.push(FIXTURES[i]); continue; }
    const n = i - FIXTURES.length + 1;
    let latex;
    if (n % 3 === 0) latex = String.raw`\frac{${n}+\sqrt{x_{${n}}^2+${n + 1}}}{1+x_{${n}}^2}+\sum_{k=1}^{${n % 9 + 2}}\frac{1}{k^2+${n}}`;
    else if (n % 3 === 1) latex = String.raw`A_{${n}}=\begin{pmatrix}${n}&1&-1\\2&${n + 1}&0\\-1&0&${n + 2}\end{pmatrix}`;
    else latex = String.raw`\lim_{x\to0}\frac{\sin(${n}x)}{x}=\int_0^{${n}} e^{-t^2}\,dt`;
    sources.push({ id: "page-" + i, category: "page", latex, displayMode: i % 4 === 0 });
  }
  return sources;
})();

function percentile(sorted, p) { return sorted[Math.max(0, Math.ceil(sorted.length * p) - 1)]; }
function summarize(name, values) {
  const sorted = [...values].filter(Number.isFinite).sort((a, b) => a - b);
  return { name, samples: sorted.length, p50Ms: percentile(sorted, .5), p95Ms: percentile(sorted, .95),
    p99Ms: percentile(sorted, .99), maximumMs: sorted[sorted.length - 1], meanMs: sorted.reduce((a, b) => a + b, 0) / sorted.length };
}
function shuffled(length) {
  const order = Array.from({ length }, (_, i) => i);
  let state = 0x19002026 >>> 0;
  for (let i = order.length - 1; i > 0; i--) { state = (Math.imul(state, 1664525) + 1013904223) >>> 0; const j = state % (i + 1); [order[i], order[j]] = [order[j], order[i]]; }
  return order;
}
function nextFrame() { return new Promise(resolve => requestAnimationFrame(() => resolve(performance.now()))); }
async function afterPaint() { await nextFrame(); return await nextFrame(); }
function syncLayout() { void stage.getBoundingClientRect(); }
async function measureParse(name, run) {
  const samples = [];
  const order = shuffled(FIXTURES.length * iterations);
  for (const index of order) {
    const fixture = FIXTURES[index % FIXTURES.length];
    const started = performance.now();
    const output = await run(fixture);
    const elapsed = performance.now() - started;
    if (!output || output.length < 10) throw new Error(fixture.id + ": empty output");
    samples.push({ category: fixture.category, ms: elapsed, bytes: output.length });
  }
  return { samples, summary: summarize(name, samples.map(s => s.ms)) };
}

window.bench = async function () {
  const initStart = performance.now();
  await vt.init();
  const initMs = performance.now() - initStart;
  await document.fonts.ready;
  // Warm WASM AOT tiers, KaTeX JIT, fonts and layout outside the timed region.
  for (let round = 0; round < 3; round++) {
    for (const fixture of FIXTURES) {
      await vt.renderToString(fixture.latex, { displayMode: fixture.displayMode, output: "svg" });
      katex.renderToString(fixture.latex, { ...OPTIONS, displayMode: fixture.displayMode, output: "html" });
    }
  }
  stage.innerHTML = "";

  // Isolate the C# parse/layout core from SVG/MathML export, JSON and DOM work.
  const compileOnly = [];
  const compileOrder = shuffled(FIXTURES.length * iterations);
  for (const index of compileOrder) {
    const fixture = FIXTURES[index % FIXTURES.length];
    const metric = await vt.compileMetrics(fixture.latex, { displayMode: fixture.displayMode });
    if (!metric.success) throw new Error(fixture.id + ": compile metrics failed");
    compileOnly.push(metric.compileMs);
  }
  const result = {
    initMs,
    compileOnly: summarize("compileOnly", compileOnly),
    compileSimple: await (async () => {
      const samples = [];
      const simpleOrder = shuffled(FIXTURES.length * Math.min(iterations, 100));
      for (const index of simpleOrder) {
        const fixture = FIXTURES[index % FIXTURES.length];
        const metric = await vt.compileMetricsSimple(fixture.latex, fixture.displayMode);
        if (!metric.success) throw new Error(fixture.id + ": simple compile failed");
        samples.push(metric.compileMs);
      }
      return summarize("compileSimple", samples);
    })(),
    svgSplit: await (async () => {
      const compile = [], exportTimes = [], lengths = [];
      for (const index of shuffled(FIXTURES.length * Math.min(iterations, 100))) {
        const fixture = FIXTURES[index % FIXTURES.length];
        const split = await vt.compileMetricsSvg(fixture.latex, { displayMode: fixture.displayMode });
        compile.push(split.compileMs); exportTimes.push(split.exportMs); lengths.push(split.length);
      }
      return { compile: summarize("compile", compile), export: summarize("export", exportTimes), outputLength: summarize("length", lengths) };
    })(),
    interopPing: await (async () => {
      const samples = [];
      for (let i = 0; i < 2000; i++) { const started = performance.now(); await vt.diagnosticPing("x"); samples.push(performance.now() - started); }
      return summarize("ping", samples);
    })(),
    parse: {
      vtSvg: await measureParse("vtSvg", f => vt.renderToString(f.latex, { displayMode: f.displayMode, output: "svg" })),
      vtMathml: await measureParse("vtMathml", f => vt.renderToString(f.latex, { displayMode: f.displayMode, output: "mathml", fastMathml: true })),
      katexHtml: await measureParse("katexHtml", f => katex.renderToString(f.latex, { displayMode: f.displayMode, output: "html" })),
      katexMathml: await measureParse("katexMathml", f => katex.renderToString(f.latex, { displayMode: f.displayMode, output: "mathml" })),
    },
    complex: {},
  };

  for (const [name, run] of [
    ["vtSvg", f => vt.renderToString(f.latex, { displayMode: f.displayMode, output: "svg" })],
    ["vtMathml", f => vt.renderToString(f.latex, { displayMode: f.displayMode, output: "mathml", fastMathml: true })],
    ["katexHtml", f => katex.renderToString(f.latex, { displayMode: f.displayMode, output: "html" })],
  ]) {
    const samples = [];
    for (let i = 0; i < iterations * 2; i++) {
      const fixture = COMPLEX[i % COMPLEX.length];
      const started = performance.now();
      await run(fixture);
      samples.push(performance.now() - started);
    }
    result.complex[name] = summarize(name, samples);
  }

  const domSync = { vt: [], katex: [] }, domPaint = { vt: [], katex: [] };
  const host = document.createElement("div");
  host.className = "row";
  stage.append(host);
  const order = shuffled(FIXTURES.length * 10);
  for (const index of order) {
    const fixture = FIXTURES[index % FIXTURES.length];
    for (const implementation of ["vt", "katex"]) {
      host.textContent = "";
      await nextFrame();
      const started = performance.now();
      if (implementation === "vt") await vt.render(fixture.latex, host, { displayMode: fixture.displayMode, throwOnError: true, output: "mathml", fastMathml: true });
      else katex.render(fixture.latex, host, { displayMode: fixture.displayMode, throwOnError: true });
      syncLayout();
      const sync = performance.now() - started;
      const painted = await afterPaint() - started;
      domSync[implementation].push(sync);
      domPaint[implementation].push(painted);
    }
  }
  result.domSync = { vt: summarize("vt", domSync.vt), katex: summarize("katex", domSync.katex) };
  result.domPaint = { vt: summarize("vt", domPaint.vt), katex: summarize("katex", domPaint.katex) };

  // Page with 100 formulas: total call-to-DOM and call-to-painted time.
  result.page = { vt: [], katex: [] };
  const pageHost = document.createElement("div");
  pageHost.id = "page";
  stage.append(pageHost);
  for (let round = 0; round < pageRounds; round++) {
    for (const implementation of round % 2 === 0 ? ["vt", "katex"] : ["katex", "vt"]) {
      pageHost.innerHTML = "";
      await nextFrame();
      const started = performance.now();
      for (const fixture of PAGE) {
        const cell = document.createElement("div");
        cell.className = "row";
        pageHost.append(cell);
        if (implementation === "vt") await vt.render(fixture.latex, cell, { displayMode: fixture.displayMode, throwOnError: true, output: "mathml", fastMathml: true });
        else katex.render(fixture.latex, cell, { displayMode: fixture.displayMode, throwOnError: true });
      }
      syncLayout();
      const sync = performance.now() - started;
      const painted = await afterPaint() - started;
      result.page[implementation].push({ sync, painted, formulas: PAGE.length });
    }
  }
  result.page.vtSummary = summarize("vt", result.page.vt.map(s => s.sync));
  result.page.katexSummary = summarize("katex", result.page.katex.map(s => s.sync));
  result.page.vtPaintSummary = summarize("vtPaint", result.page.vt.map(s => s.painted));
  result.page.katexPaintSummary = summarize("katexPaint", result.page.katex.map(s => s.painted));

  result.crossOriginIsolated = crossOriginIsolated;
  result.timerResolution = (() => { const a = performance.now(); let b = a; for (let i = 0; i < 10000; i++) b = performance.now(); return b - a; })();
  result.frameInterval = await (async () => {
    const values = [];
    for (let i = 0; i < 20; i++) { const a = await nextFrame(); const b = await nextFrame(); values.push(b - a); }
    return summarize("frame", values);
  })();
  result.sampleOutput = (await vt.renderToString(String.raw`\frac{a}{b}`, { output: "svg" })).slice(0, 120);
  return result;
};
window.__ready = true;
