import * as vt from '/vt/index.mjs';

const parameters = new URLSearchParams(location.search);
const ENGINE = parameters.get('engine') ?? 'velocitex';
const stage = document.getElementById('stage');
let corpus;

function percentile(sorted, p) {
  return sorted[Math.max(0, Math.ceil(sorted.length * p) - 1)];
}
function summarize(name, values) {
  const sorted = [...values].filter(Number.isFinite).sort((a, b) => a - b);
  if (!sorted.length) return { name, samples: 0 };
  return {
    name,
    samples: sorted.length,
    p50Ms: percentile(sorted, 0.5),
    p95Ms: percentile(sorted, 0.95),
    p99Ms: percentile(sorted, 0.99),
    maximumMs: sorted[sorted.length - 1],
    meanMs: sorted.reduce((a, b) => a + b, 0) / sorted.length,
    totalMs: sorted.reduce((a, b) => a + b, 0),
  };
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
function nextFrame() {
  return new Promise((resolve) => requestAnimationFrame((time) => resolve(time)));
}
async function afterPaint() {
  await nextFrame();
  return await nextFrame();
}
function flushLayout() {
  void stage.getBoundingClientRect();
}

function formulasOf(workload) {
  const cases = corpus.workloads[workload].cases;
  if (workload !== 'page')
    return cases.map((item) => ({ id: item.id, latex: item.latex, displayMode: item.displayMode }));
  const formulas = [];
  for (const page of cases) {
    let index = 0;
    for (const paragraph of page.paragraphs) {
      for (const segment of paragraph) {
        if (segment.latex !== undefined)
          formulas.push({
            id: `${page.id}-${index++}`,
            latex: segment.latex,
            displayMode: segment.displayMode,
            page: page.id,
          });
      }
    }
  }
  return formulas;
}

async function renderToString(source, options, output) {
  if (ENGINE === 'katex') {
    if (output === 'svg') return null;
    return katex.renderToString(source, { ...options, output });
  }
  return vt.renderToString(source, { ...options, output, fastMathml: output !== 'svg' });
}

async function renderInto(source, element, options) {
  if (ENGINE === 'katex') {
    katex.render(source, element, options);
    return;
  }
  await vt.render(source, element, options);
}

function displayOptions(displayMode) {
  return ENGINE === 'katex'
    ? { displayMode, throwOnError: true, trust: false, strict: 'ignore', maxExpand: 1000 }
    : { displayMode, throwOnError: true, trust: false, strict: 'ignore', maxExpand: 1000 };
}

async function measureParse(workload, output, repetitions) {
  const cases = corpus.workloads[workload].cases;
  const samples = [];
  if (workload === 'page') {
    const formulas = formulasOf('page');
    for (const page of cases) {
      const pageFormulas = formulas.filter((item) => item.page === page.id);
      for (let round = 0; round < repetitions; round++) {
        const started = performance.now();
        let bytes = 0;
        for (const item of pageFormulas) {
          const markup = await renderToString(item.latex, displayOptions(item.displayMode), output);
          if (markup === null) return { samples: [], summary: { name: output, samples: 0, unsupported: true } };
          bytes += markup.length;
        }
        samples.push({ id: page.id, formulas: pageFormulas.length, ms: performance.now() - started, bytes });
      }
    }
  } else {
    const order = shuffled(cases.length * repetitions);
    for (const index of order) {
      const item = cases[index % cases.length];
      const started = performance.now();
      const markup = await renderToString(item.latex, displayOptions(item.displayMode), output);
      const ms = performance.now() - started;
      if (markup === null) return { samples: [], summary: { name: output, samples: 0, unsupported: true } };
      if (!markup || markup.length < 10) throw new Error(`${item.id}: empty ${output} output`);
      samples.push({ id: item.id, ms, bytes: markup.length });
    }
  }
  return {
    samples,
    summary: summarize(
      output,
      samples.map((sample) => sample.ms)
    ),
  };
}

async function measureSingleEndToEnd(workload, repetitions) {
  const cases = corpus.workloads[workload].cases;
  const order = shuffled(cases.length * repetitions);
  const samples = [];
  for (const index of order) {
    const item = cases[index % cases.length];
    stage.innerHTML = '';
    const host = document.createElement('div');
    host.className = 'row';
    stage.append(host);
    await nextFrame();
    const started = performance.now();
    await renderInto(item.latex, host, displayOptions(item.displayMode));
    flushLayout();
    const syncMs = performance.now() - started;
    const paintedMs = (await afterPaint()) - started;
    samples.push({ id: item.id, syncMs, paintedMs });
  }
  return {
    samples,
    summary: summarize(
      'painted',
      samples.map((sample) => sample.paintedMs)
    ),
    syncSummary: summarize(
      'sync',
      samples.map((sample) => sample.syncMs)
    ),
  };
}

function buildPage(page) {
  stage.innerHTML = '';
  const title = document.createElement('div');
  title.className = 'page-title';
  title.textContent = page.title;
  stage.append(title);
  const spans = [];
  for (const paragraph of page.paragraphs) {
    const p = document.createElement('p');
    p.className = 'para';
    for (const segment of paragraph) {
      if (segment.latex !== undefined) {
        const span = document.createElement('span');
        span.className = 'math';
        p.append(span);
        spans.push({ element: span, latex: segment.latex, displayMode: segment.displayMode });
      } else {
        p.append(document.createTextNode(segment.text));
      }
    }
    stage.append(p);
  }
  return spans;
}

async function measurePageEndToEnd(repetitions) {
  const pages = corpus.workloads.page.cases;
  const samples = [];
  for (const page of pages) {
    for (let round = 0; round < repetitions; round++) {
      const spans = buildPage(page);
      await afterPaint();
      const started = performance.now();
      for (const span of spans) await renderInto(span.latex, span.element, displayOptions(span.displayMode));
      flushLayout();
      const syncMs = performance.now() - started;
      const paintedMs = (await afterPaint()) - started;
      samples.push({
        id: page.id,
        formulas: spans.length,
        syncMs,
        paintedMs,
        perFormulaPaintedMs: paintedMs / spans.length,
      });
    }
  }
  return {
    samples,
    summary: summarize(
      'painted',
      samples.map((sample) => sample.paintedMs)
    ),
    syncSummary: summarize(
      'sync',
      samples.map((sample) => sample.syncMs)
    ),
    perFormulaSummary: summarize(
      'perFormula',
      samples.map((sample) => sample.perFormulaPaintedMs)
    ),
  };
}

window.runBenchmark = async function (config) {
  corpus = await (await fetch('/workloads.json')).json();
  let initMs = 0;
  if (ENGINE === 'velocitex') {
    const started = performance.now();
    await vt.init();
    initMs = performance.now() - started;
  }
  await document.fonts.ready;
  await afterPaint();

  // Warm every path outside the timed region: JIT tiers, WASM module
  // instantiation, KaTeX CSS/font loading and browser math layout.
  for (let round = 0; round < 3; round++) {
    for (const workload of ['single', 'long', 'page']) {
      for (const item of formulasOf(workload)) {
        await renderToString(item.latex, displayOptions(item.displayMode), 'mathml');
        if (ENGINE === 'velocitex') await renderToString(item.latex, displayOptions(item.displayMode), 'svg');
      }
    }
  }
  stage.innerHTML = '';
  const warmHost = document.createElement('div');
  stage.append(warmHost);
  await renderInto(formulasOf('single')[0].latex, warmHost, displayOptions(false));
  await afterPaint();

  const result = { engine: ENGINE, initMs, parse: {}, endToEnd: {} };
  for (const workload of ['single', 'long', 'page']) {
    const repetitions = workload === 'page' ? config.pageRounds : config.iterations;
    result.parse[workload] = { mathml: await measureParse(workload, 'mathml', repetitions) };
    result.parse[workload].svg =
      ENGINE === 'katex' ? null : await measureParse(workload, 'svg', Math.max(2, Math.min(repetitions, 20)));
  }
  result.endToEnd.single = await measureSingleEndToEnd('single', config.e2eIterations);
  result.endToEnd.long = await measureSingleEndToEnd('long', config.e2eIterations);
  result.endToEnd.page = await measurePageEndToEnd(config.pageRounds);
  result.frameInterval = await (async () => {
    const values = [];
    for (let i = 0; i < 20; i++) {
      const a = await nextFrame();
      const b = await nextFrame();
      values.push(b - a);
    }
    return summarize('frame', values);
  })();
  result.crossOriginIsolated = crossOriginIsolated;
  result.fontStatus = document.fonts.status;
  result.katexVersion = globalThis.katex?.version ?? null;
  result.pageScreenshot = true;
  return result;
};

window.__benchReady = true;
