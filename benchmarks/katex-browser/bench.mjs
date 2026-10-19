// Dependency-free KaTeX 0.19.0 browser benchmark.
// Serves the exact locked npm dist, drives a local Chrome through the DevTools
// protocol, and records parse-to-markup plus DOM render/frame timings.
//
// Usage:
//   node bench.mjs --out artifacts/performance/katex --iterations 300 --e2e-iterations 12
// The KaTeX package and Chrome executable are external, locked inputs; see README.md.

import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import { tmpdir } from "node:os";
import path from "node:path";
import process from "node:process";

const root = path.resolve(new URL("../..", import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, "$1"));
const args = process.argv.slice(2);
const option = (name, fallback) => {
  const i = args.indexOf(name);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : fallback;
};
const outDir = path.resolve(option("--out", path.join(root, "artifacts/performance/katex")));
const iterations = Number(option("--iterations", "300"));
const e2eIterations = Number(option("--e2e-iterations", "12"));
const katexDir = path.resolve(option("--katex-dir", path.join(root, "artifacts/tooling/katex/package")));
const chromePath = option("--chrome", "C:/Program Files/Google/Chrome/Application/chrome.exe");
const corpusPath = path.join(root, "benchmarks/corpus.normal.json");
const headless = !args.includes("--headful");

const corpusBytes = await readFile(corpusPath);
const corpus = JSON.parse(corpusBytes.toString("utf8"));
const fixtures = corpus.fixtures;
if (fixtures.length !== 60 || new Set(fixtures.map((f) => f.category)).size !== 6) {
  throw new Error("The frozen normal corpus changed shape.");
}
const corpusSha256 = createHash("sha256").update(corpusBytes).digest("hex");
const katexVersion = JSON.parse(await readFile(path.join(katexDir, "package.json"), "utf8")).version;
if (katexVersion !== "0.19.0") throw new Error(`KaTeX version ${katexVersion} is not the locked baseline.`);
await mkdir(outDir, { recursive: true });

const page = `<!doctype html>
<html><head><meta charset="utf-8">
<link rel="stylesheet" href="/dist/katex.min.css">
<style>
  html,body{margin:0;padding:0;background:#fff;color:#111}
  #stage{position:relative;width:900px;min-height:300px;padding:8px;font-size:24px;overflow:hidden}
  #target{position:relative}
</style></head>
<body><div id="stage"><div id="target"></div></div>
<script src="/dist/katex.min.js"></script>
<script>
const FIXTURES = ${JSON.stringify(fixtures.map((f) => ({ id: f.id, category: f.category, latex: f.latex, displayMode: f.displayMode })))};
const OPTIONS = { throwOnError: true, trust: false, maxExpand: 1000, maxSize: 20, strict: "ignore" };
const stage = document.getElementById("stage");
const target = document.getElementById("target");
function percentile(sorted, p) { return sorted[Math.max(0, Math.ceil(sorted.length * p) - 1)]; }
function summarize(name, values) {
  const sorted = [...values].sort((a, b) => a - b);
  return { name, samples: sorted.length, p50Ms: percentile(sorted, .5), p95Ms: percentile(sorted, .95),
    p99Ms: percentile(sorted, .99), maximumMs: sorted[sorted.length - 1],
    meanMs: sorted.reduce((a, b) => a + b, 0) / sorted.length };
}
function shuffled(length) {
  const order = Array.from({ length }, (_, i) => i);
  let state = 0x19002026 >>> 0;
  for (let i = order.length - 1; i > 0; i--) {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
    const j = state % (i + 1);
    [order[i], order[j]] = [order[j], order[i]];
  }
  return order;
}
function renderMarkup(fixture, output) {
  return katex.renderToString(fixture.latex, { ...OPTIONS, displayMode: fixture.displayMode, output });
}
function nextFrame() { return new Promise((resolve) => requestAnimationFrame(() => resolve(performance.now()))); }
async function afterPaint() {
  await nextFrame();
  const first = await nextFrame();
  return first;
}
window.runBenchmark = async function (config) {
  await document.fonts.ready;
  await new Promise((r) => requestAnimationFrame(() => r()));
  // Warm JIT, CSS, glyph caches and font loading outside the timed region.
  for (let round = 0; round < 3; round++) {
    for (const fixture of FIXTURES) { renderMarkup(fixture, "html"); renderMarkup(fixture, "mathml"); }
  }
  target.innerHTML = "";
  katex.render(FIXTURES[0].latex, target, { ...OPTIONS, displayMode: FIXTURES[0].displayMode });
  await document.fonts.ready;
  await afterPaint();

  const result = { parse: {}, domSync: {}, domPaint: {} };
  for (const output of ["html", "mathml"]) {
    const samples = [];
    const order = shuffled(FIXTURES.length * config.iterations);
    for (let i = 0; i < order.length; i++) {
      const fixture = FIXTURES[order[i] % FIXTURES.length];
      const start = performance.now();
      const markup = renderMarkup(fixture, output);
      const elapsed = performance.now() - start;
      if (!markup || markup.length < 10) throw new Error(fixture.id + ": empty markup");
      samples.push({ category: fixture.category, ms: elapsed, bytes: markup.length });
    }
    result.parse[output] = { samples, summary: summarize(output, samples.map((s) => s.ms)) };
  }

  const domSync = [], domPaint = [];
  const order = shuffled(config.e2eIterations * FIXTURES.length);
  for (let i = 0; i < order.length; i++) {
    const fixture = FIXTURES[order[i] % FIXTURES.length];
    target.textContent = "";
    const start = performance.now();
    katex.render(fixture.latex, target, { ...OPTIONS, displayMode: fixture.displayMode });
    void target.getBoundingClientRect();
    const sync = performance.now() - start;
    const painted = await afterPaint() - start;
    domSync.push({ category: fixture.category, ms: sync });
    domPaint.push({ category: fixture.category, ms: painted });
  }
  result.domSync = { samples: domSync, summary: summarize("domSync", domSync.map((s) => s.ms)) };
  result.domPaint = { samples: domPaint, summary: summarize("domPaint", domPaint.map((s) => s.ms)) };
  result.frameInterval = await (async () => {
    const values = [];
    for (let i = 0; i < 20; i++) { const a = await nextFrame(); const b = await nextFrame(); values.push(b - a); }
    return summarize("frameInterval", values);
  })();
  result.glyphBox = target.getBoundingClientRect().toJSON();
  result.fontStatus = document.fonts.status;
  return result;
};
window.__benchReady = true;
</script></body></html>`;

const contentTypes = { ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8", ".woff2": "font/woff2", ".woff": "font/woff", ".ttf": "font/ttf" };
const isolationHeaders = {
  "cross-origin-opener-policy": "same-origin",
  "cross-origin-embedder-policy": "require-corp",
  "cross-origin-resource-policy": "same-origin",
  "cache-control": "no-store",
};
const server = createServer(async (req, res) => {
  try {
    const url = new URL(req.url, "http://127.0.0.1");
    if (url.pathname === "/" || url.pathname === "/index.html") {
      res.writeHead(200, { "content-type": "text/html; charset=utf-8", ...isolationHeaders });
      res.end(page);
      return;
    }
    if (url.pathname === "/dist/katex.min.js" || url.pathname === "/dist/katex.min.css" || url.pathname.startsWith("/dist/fonts/")) {
      const relative = url.pathname.slice("/dist/".length);
      if (relative.includes("..")) throw new Error("bad path");
      const bytes = await readFile(path.join(katexDir, "dist", relative));
      res.writeHead(200, { "content-type": contentTypes[path.extname(relative)] ?? "application/octet-stream", ...isolationHeaders });
      res.end(bytes);
      return;
    }
    res.writeHead(404); res.end();
  } catch (error) {
    res.writeHead(500); res.end(String(error));
  }
});
await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
const serverPort = server.address().port;
const pageUrl = `http://127.0.0.1:${serverPort}/`;
const profile = path.join(root, "artifacts", "tooling", `chrome-profile-${process.pid}-${Date.now()}`);
await mkdir(profile, { recursive: true });
const debugPort = 9300 + (process.pid % 500);
const chromeArgs = [
  `--remote-debugging-port=${debugPort}`, "--remote-debugging-address=127.0.0.1",
  "--remote-allow-origins=*", "--no-sandbox", "--disable-breakpad", "--disable-crash-reporter",
  `--user-data-dir=${profile}`, "--no-first-run", "--no-default-browser-check",
  "--disable-background-networking", "--disable-background-timer-throttling",
  "--disable-backgrounding-occluded-windows", "--disable-renderer-backgrounding",
  "--disable-features=CalculateNativeWinOcclusion", "--disable-extensions",
  "--hide-scrollbars", "--window-size=1200,900", "--force-device-scale-factor=1",
  ...(headless ? ["--headless=new"] : []),
  pageUrl,
];
const chrome = spawn(chromePath, chromeArgs, { stdio: "ignore" });
let exited = false;
chrome.on("exit", () => { exited = true; });

async function waitForTargets() {
  for (let attempt = 0; attempt < 200; attempt++) {
    try {
      const response = await fetch(`http://127.0.0.1:${debugPort}/json/list`);
      const targets = await response.json();
      const target = targets.find((t) => t.type === "page" && t.url.startsWith(pageUrl));
      if (target) return target;
    } catch { /* Chrome is still starting. */ }
    await new Promise((r) => setTimeout(r, 100));
  }
  throw new Error("Chrome did not expose the benchmark page.");
}

let ws;
try {
  const target = await waitForTargets();
  ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
  let nextId = 1;
  const pending = new Map();
  ws.onmessage = (event) => {
    const message = JSON.parse(event.data);
    if (!message.id) return;
    const entry = pending.get(message.id);
    if (!entry) return;
    pending.delete(message.id);
    if (message.error) entry.reject(new Error(JSON.stringify(message.error))); else entry.resolve(message.result);
  };
  const send = (method, params = {}) => new Promise((resolve, reject) => {
    const id = nextId++;
    pending.set(id, { resolve, reject });
    ws.send(JSON.stringify({ id, method, params }));
  });
  const evaluate = async (expression) => {
    const result = await send("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true });
    if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description ?? JSON.stringify(result.exceptionDetails));
    return result.result.value;
  };
  await send("Page.enable");
  for (let attempt = 0; attempt < 100; attempt++) {
    try { if (await evaluate("window.__benchReady === true")) break; } catch { /* document not ready */ }
    await new Promise((r) => setTimeout(r, 100));
    if (attempt === 99) throw new Error("benchmark page did not become ready");
  }
  const hardware = await evaluate("({ userAgent: navigator.userAgent, devicePixelRatio, cores: navigator.hardwareConcurrency, memory: navigator.deviceMemory ?? null, crossOriginIsolated, timerResolution: (() => { const a = performance.now(); let b = a; for (let i = 0; i < 10000; i++) b = performance.now(); return b - a; })() })");
  const started = performance.now();
  const result = await evaluate(`window.runBenchmark(${JSON.stringify({ iterations, e2eIterations })})`);
  const wallMs = performance.now() - started;
  const screenshot = await send("Page.captureScreenshot", { format: "png", captureBeyondViewport: false });
  await writeFile(path.join(outDir, "katex-render.png"), Buffer.from(screenshot.data, "base64"));
  const report = {
    kind: "KaTeX browser benchmark; frozen normal corpus",
    utc: new Date().toISOString(),
    katexVersion, corpusSha256,
    headless, chrome: hardware, iterations, e2eIterations,
    frameWaitNote: "domPaint waits for the second animation frame after a forced layout, so the frame containing the DOM change has been painted; it includes frame pacing and is reported separately from domSync and parse markup time.",
    result, wallMs,
  };
  await writeFile(path.join(outDir, "katex-report.json"), JSON.stringify(report, null, 2));
  console.log(`KaTeX ${katexVersion} parse(html) p50=${result.parse.html.summary.p50Ms.toFixed(4)}ms p95=${result.parse.html.summary.p95Ms.toFixed(4)}ms`);
  console.log(`KaTeX ${katexVersion} parse(mathml) p50=${result.parse.mathml.summary.p50Ms.toFixed(4)}ms p95=${result.parse.mathml.summary.p95Ms.toFixed(4)}ms`);
  console.log(`KaTeX DOM sync p50=${result.domSync.summary.p50Ms.toFixed(3)}ms p95=${result.domSync.summary.p95Ms.toFixed(3)}ms`);
  console.log(`KaTeX DOM painted p50=${result.domPaint.summary.p50Ms.toFixed(3)}ms p95=${result.domPaint.summary.p95Ms.toFixed(3)}ms`);
  console.log(`frame interval p50=${result.frameInterval.p50Ms.toFixed(3)}ms; output=${outDir}`);
} finally {
  try { ws?.close(); } catch { /* ignore */ }
  if (!exited) chrome.kill();
  server.close();
  await new Promise((r) => setTimeout(r, 300));
  await rm(profile, { recursive: true, force: true }).catch(() => {});
}
