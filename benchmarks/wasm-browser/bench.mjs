// Dependency-free browser benchmark: VelociTex WASM (SVG+MathML) versus KaTeX 0.19.0.
// Covers single-formula parse, complex formula parse, single DOM render and 100-formula pages.
//
// Usage:
//   node bench.mjs --out artifacts/performance/wasm --iterations 200 --page-rounds 5

import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import path from "node:path";
import process from "node:process";

const root = path.resolve(new URL("../..", import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, "$1"));
const args = process.argv.slice(2);
const option = (name, fallback) => { const i = args.indexOf(name); return i >= 0 && i + 1 < args.length ? args[i + 1] : fallback; };
const outDir = path.resolve(option("--out", path.join(root, "artifacts/performance/wasm")));
const iterations = Number(option("--iterations", "200"));
const pageRounds = Number(option("--page-rounds", "5"));
const katexDir = path.resolve(option("--katex-dir", path.join(root, "artifacts/tooling/katex/package")));
const vtDist = path.resolve(option("--vt-dist", path.join(root, "npm/velocitex/dist")));
const chromePath = option("--chrome", "C:/Program Files/Google/Chrome/Application/chrome.exe");
const corpusPath = path.join(root, "benchmarks/corpus.normal.json");
const pageDir = path.join(root, "benchmarks/wasm-browser");
const headless = !args.includes("--headful");

const corpusBytes = await readFile(corpusPath);
const corpusSha256 = createHash("sha256").update(corpusBytes).digest("hex");
const vtIndex = await readFile(path.join(vtDist, "index.mjs"), "utf8");
if (!vtIndex.includes("renderToString")) throw new Error("VelociTex dist is missing; run build-wasm.ps1.");
await mkdir(outDir, { recursive: true });

const contentTypes = { ".mjs": "text/javascript", ".js": "text/javascript", ".html": "text/html; charset=utf-8", ".css": "text/css", ".json": "application/json", ".wasm": "application/wasm", ".woff2": "font/woff2", ".woff": "font/woff", ".ttf": "font/ttf" };
const isolationHeaders = { "cross-origin-opener-policy": "same-origin", "cross-origin-embedder-policy": "require-corp", "cross-origin-resource-policy": "same-origin", "cache-control": "no-store" };
const server = createServer(async (req, res) => {
  try {
    const url = new URL(req.url, "http://127.0.0.1");
    if (url.pathname === "/" || url.pathname === "/index.html") {
      res.writeHead(200, { "content-type": contentTypes[".html"], ...isolationHeaders });
      res.end(await readFile(path.join(pageDir, "page.html")));
      return;
    }
    if (url.pathname === "/page.js") {
      res.writeHead(200, { "content-type": contentTypes[".js"], ...isolationHeaders });
      res.end(await readFile(path.join(pageDir, "page.js")));
      return;
    }
    if (url.pathname === "/corpus.json") {
      res.writeHead(200, { "content-type": contentTypes[".json"], ...isolationHeaders });
      res.end(corpusBytes);
      return;
    }
    let file;
    if (url.pathname.startsWith("/katex/")) file = path.join(katexDir, "dist", url.pathname.slice("/katex/".length));
    else if (url.pathname.startsWith("/vt/")) file = path.join(vtDist, url.pathname.slice("/vt/".length));
    else { res.writeHead(404); res.end(); return; }
    const extension = path.extname(file);
    res.writeHead(200, { "content-type": contentTypes[extension] ?? "application/octet-stream", ...isolationHeaders });
    res.end(await readFile(file));
  } catch { res.writeHead(404); res.end(); }
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
const pageUrl = `http://127.0.0.1:${server.address().port}/?iterations=${iterations}&pageRounds=${pageRounds}`;
const profile = path.join(root, "artifacts", "tooling", `chrome-wasm-${process.pid}-${Date.now()}`);
await mkdir(profile, { recursive: true });
const debugPort = 9800 + (process.pid % 100);
const chrome = spawn(chromePath, [`--remote-debugging-port=${debugPort}`, "--remote-debugging-address=127.0.0.1", "--remote-allow-origins=*",
  "--no-sandbox", "--disable-breakpad", "--disable-crash-reporter", `--user-data-dir=${profile}`, "--no-first-run", "--no-default-browser-check",
  "--disable-background-timer-throttling", "--disable-backgrounding-occluded-windows", "--disable-renderer-backgrounding",
  "--disable-features=CalculateNativeWinOcclusion", "--hide-scrollbars", "--window-size=1200,1000", "--force-device-scale-factor=1",
  ...(headless ? ["--headless=new"] : []), pageUrl], { stdio: "ignore" });
let ws;
try {
  let target;
  for (let attempt = 0; attempt < 300; attempt++) {
    try {
      const targets = await (await fetch(`http://127.0.0.1:${debugPort}/json/list`)).json();
      target = targets.find(t => t.type === "page" && t.url.startsWith(`http://127.0.0.1:${server.address().port}/`));
      if (target) break;
    } catch { /* Chrome is still starting. */ }
    await new Promise(r => setTimeout(r, 100));
  }
  if (!target) throw new Error("Chrome did not expose the benchmark page.");
  ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
  let nextId = 1;
  const pending = new Map();
  ws.onmessage = event => { const message = JSON.parse(event.data); if (!message.id) return; const entry = pending.get(message.id); if (!entry) return; pending.delete(message.id); if (message.error) entry.reject(new Error(JSON.stringify(message.error))); else entry.resolve(message.result); };
  const send = (method, params = {}) => new Promise((resolve, reject) => { const id = nextId++; pending.set(id, { resolve, reject }); ws.send(JSON.stringify({ id, method, params })); });
  const evaluate = async expression => { const result = await send("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true }); if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description ?? JSON.stringify(result.exceptionDetails)); return result.result.value; };
  await send("Page.enable");
  for (let attempt = 0; attempt < 300; attempt++) {
    try { if (await evaluate("window.__ready === true")) break; } catch { /* loading */ }
    if (attempt === 299) throw new Error("benchmark page did not become ready");
    await new Promise(r => setTimeout(r, 100));
  }
  const hardware = await evaluate("({ userAgent: navigator.userAgent, cores: navigator.hardwareConcurrency, devicePixelRatio, crossOriginIsolated })");
  const started = performance.now();
  const result = await evaluate("window.bench()");
  const wallMs = performance.now() - started;
  const screenshot = await send("Page.captureScreenshot", { format: "png", captureBeyondViewport: false });
  await writeFile(path.join(outDir, "wasm-browser.png"), Buffer.from(screenshot.data, "base64"));
  const report = { kind: "VelociTex WASM vs KaTeX 0.19.0 browser benchmark; frozen normal corpus plus 100-formula page", utc: new Date().toISOString(), corpusSha256, iterations, pageRounds, headless, chrome: hardware, wallMs, result };
  await writeFile(path.join(outDir, "wasm-report.json"), JSON.stringify(report, null, 2));
  const p = result.parse;
  console.log(`VT   parse svg    p50=${p.vtSvg.summary.p50Ms.toFixed(4)} mean=${p.vtSvg.summary.meanMs.toFixed(4)} p95=${p.vtSvg.summary.p95Ms.toFixed(4)}`);
  console.log(`VT   parse mathml p50=${p.vtMathml.summary.p50Ms.toFixed(4)} mean=${p.vtMathml.summary.meanMs.toFixed(4)} p95=${p.vtMathml.summary.p95Ms.toFixed(4)}`);
  console.log(`KTX  parse html   p50=${p.katexHtml.summary.p50Ms.toFixed(4)} mean=${p.katexHtml.summary.meanMs.toFixed(4)} p95=${p.katexHtml.summary.p95Ms.toFixed(4)}`);
  console.log(`KTX  parse mathml p50=${p.katexMathml.summary.p50Ms.toFixed(4)} mean=${p.katexMathml.summary.meanMs.toFixed(4)} p95=${p.katexMathml.summary.p95Ms.toFixed(4)}`);
  console.log(`DOM sync  vt=${result.domSync.vt.p50Ms.toFixed(3)}ms katex=${result.domSync.katex.p50Ms.toFixed(3)}ms; page sync vt=${result.page.vtSummary.p50Ms.toFixed(3)}ms katex=${result.page.katexSummary.p50Ms.toFixed(3)}ms; output=${outDir}`);
} finally {
  try { ws?.close(); } catch { /* ignore */ }
  chrome.kill(); server.close();
  await new Promise(r => setTimeout(r, 300));
  await rm(profile, { recursive: true, force: true }).catch(() => {});
}
