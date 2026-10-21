import * as vt from '/vt/index.mjs';
import { katexMathAvailable } from '/vt/katexmath.mjs';

const ENGINE = new URLSearchParams(location.search).get('engine') ?? 'velocitex';
const stage = document.getElementById('stage');
const pause = () => new Promise((resolve) => setTimeout(resolve, 0));
let checksum = 0;

function summarize(values) {
  const sorted = [...values].sort((a, b) => a - b);
  return {
    samples: sorted.length,
    medianMs: sorted[Math.floor(sorted.length / 2)],
    meanMs: sorted.reduce((a, b) => a + b, 0) / sorted.length,
    minimumMs: sorted[0],
    maximumMs: sorted.at(-1),
  };
}

// Full public API calls, including Promise overhead. Batch distributions are
// throughput estimates, never individual-call P95/P99 latencies.
async function measure(operation, config) {
  const batch = async (count) => {
    const start = performance.now();
    for (let i = 0; i < count; i++) checksum = (checksum + (await operation())) | 0;
    return performance.now() - start;
  };
  let count = 1;
  let elapsed;
  do {
    elapsed = await batch(count);
    if (elapsed < config.batchMs) count *= 2;
  } while (elapsed < config.batchMs);
  const batches = [];
  for (let i = 0; i < config.samples; i++) {
    await pause();
    const totalMs = await batch(count);
    batches.push({ operations: count, totalMs, msPerOperation: totalMs / count });
  }
  return { operationsPerBatch: count, batches, summary: summarize(batches.map((b) => b.msPerOperation)) };
}

function units(corpus, name) {
  return corpus.workloads[name].cases.map((item) => {
    if (name !== 'page') return { id: item.id, formulas: [item] };
    return {
      id: item.id,
      title: item.title,
      paragraphs: item.paragraphs,
      formulas: item.paragraphs.flat().filter((s) => s.latex !== undefined),
    };
  });
}

function hosts(unit) {
  stage.replaceChildren();
  const targets = [];
  if (unit.paragraphs) {
    const title = document.createElement('h2');
    title.textContent = unit.title;
    stage.append(title);
    for (const paragraph of unit.paragraphs) {
      const p = document.createElement('p');
      p.className = 'para';
      for (const segment of paragraph) {
        if (segment.latex === undefined) p.append(document.createTextNode(segment.text));
        else {
          const span = document.createElement('span');
          span.className = 'math';
          p.append(span);
          targets.push(span);
        }
      }
      stage.append(p);
    }
  } else {
    const host = document.createElement('div');
    host.style.fontSize = '24px';
    stage.append(host);
    targets.push(host);
  }
  return targets;
}

window.runBenchmark = async function (config) {
  const corpus = await (await fetch('/workloads.json')).json();
  let build = null;
  const routes = {};
  if (ENGINE === 'velocitex') {
    build = await (await fetch('/vt/build-info.json')).json();
    if (build.configuration !== 'Release' || !build.aot) throw new Error('Use the Release AOT distribution.');
    await vt.init();
    if (!katexMathAvailable()) throw new Error('HTML/CSS WASM module unavailable.');
  }
  const result = { engine: ENGINE, build, config, outputContract: 'htmlAndMathml', workloads: {}, routes };
  const start = performance.now();
  // Use the public default HTML+accessibility MathML output on BOTH libraries.
  for (const name of ['single', 'long', 'page']) {
    const records = [];
    for (const unit of units(corpus, name)) {
      const targets = hosts(unit);
      const options = unit.formulas.map((f) => ({ displayMode: f.displayMode, throwOnError: true }));
      for (const [i, f] of unit.formulas.entries()) {
        const actual =
          ENGINE === 'katex' ? katex.renderToString(f.latex, options[i]) : await vt.renderToString(f.latex, options[i]);
        if (actual !== katex.renderToString(f.latex, options[i]))
          throw new Error(`${unit.id}-${i}: default HTML differs from KaTeX`);
        routes[`${unit.id}-${i}`] = ENGINE === 'katex' ? 'javascript' : 'katex-html-wasm';
      }
      const html = async () => {
        let size = 0;
        for (const [i, f] of unit.formulas.entries()) {
          const markup =
            ENGINE === 'katex'
              ? katex.renderToString(f.latex, options[i])
              : await vt.renderToString(f.latex, options[i]);
          if (!markup.includes('katex-html')) throw new Error(`${unit.id}: invalid HTML`);
          size += markup.length;
        }
        return size;
      };
      const mathml = async () => {
        let size = 0;
        for (const [i, f] of unit.formulas.entries()) {
          const opts = { ...options[i], output: 'mathml' };
          const markup =
            ENGINE === 'katex' ? katex.renderToString(f.latex, opts) : await vt.renderToString(f.latex, opts);
          if (!markup.includes('<math')) throw new Error(`${unit.id}: invalid MathML`);
          size += markup.length;
        }
        return size;
      };
      const dom = async () => {
        for (const [i, f] of unit.formulas.entries()) {
          if (ENGINE === 'katex') katex.render(f.latex, targets[i], options[i]);
          else await vt.render(f.latex, targets[i], options[i]);
          // Force layout after EVERY formula: replacements cannot coalesce.
          const rect = targets[i].getBoundingClientRect();
          if (!(rect.width > 0 && rect.height > 0)) throw new Error(`${unit.id}: empty layout`);
        }
        return targets.length;
      };
      await html();
      await mathml();
      await dom();
      await document.fonts.ready;
      const warm = performance.now();
      do {
        await html();
        await mathml();
        await dom();
      } while (performance.now() - warm < config.warmupMs);
      records.push({
        id: unit.id,
        formulaCount: unit.formulas.length,
        html: await measure(html, config),
        mathml: await measure(mathml, config),
        dom: await measure(dom, config),
      });
    }
    result.workloads[name] = records;
  }
  result.wallMs = performance.now() - start;
  result.checksum = checksum;
  return result;
};
window.__benchReady = true;
