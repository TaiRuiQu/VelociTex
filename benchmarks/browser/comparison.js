import * as vt from '/vt/index.mjs';

const formulas = await (await fetch('/comparison.json')).json();
const grid = document.getElementById('grid');
const cells = new Map();
const validation = [];

for (const formula of formulas) {
  const row = document.createElement('div');
  row.className = 'row';
  const wasm = document.createElement('div');
  wasm.className = 'cell';
  wasm.id = `${formula.id}-wasm`;
  const katexCell = document.createElement('div');
  katexCell.className = 'cell';
  katexCell.id = `${formula.id}-katex`;
  row.append(wasm, katexCell);
  grid.append(row);
  cells.set(`${formula.id}-wasm`, wasm);
  cells.set(`${formula.id}-katex`, katexCell);
}

await vt.init();
await document.fonts.ready;

for (const formula of formulas) {
  await vt.render(formula.latex, cells.get(`${formula.id}-wasm`), {
    displayMode: formula.displayMode,
    throwOnError: true,
  });
  katex.render(formula.latex, cells.get(`${formula.id}-katex`), {
    displayMode: formula.displayMode,
    throwOnError: true,
  });
  const actual = await vt.renderToString(formula.latex, {
    displayMode: formula.displayMode,
    throwOnError: true,
  });
  const expected = katex.renderToString(formula.latex, { displayMode: formula.displayMode, throwOnError: true });
  let firstDifference = 0;
  while (firstDifference < actual.length && actual[firstDifference] === expected[firstDifference]) firstDifference++;
  validation.push({
    id: formula.id,
    htmlIdentical: actual === expected,
    actualLength: actual.length,
    expectedLength: expected.length,
    firstDifference: actual === expected ? null : firstDifference,
    actualSnippet: actual === expected ? null : actual.slice(Math.max(0, firstDifference - 40), firstDifference + 120),
    expectedSnippet:
      actual === expected ? null : expected.slice(Math.max(0, firstDifference - 40), firstDifference + 120),
  });
}
await document.fonts.ready;
await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));

for (const entry of validation) {
  const nodes = (engine) => [...cells.get(`${entry.id}-${engine}`).querySelector('.katex-html').querySelectorAll('*')];
  const actual = nodes('wasm');
  const expected = nodes('katex');
  entry.computedStyleDifferences = [];
  entry.layoutDifferences = [];
  if (actual.length !== expected.length) throw new Error(`${entry.id}: different element counts`);
  for (let i = 0; i < actual.length; i++) {
    const a = getComputedStyle(actual[i]);
    const b = getComputedStyle(expected[i]);
    for (const property of a) {
      if (a.getPropertyValue(property) !== b.getPropertyValue(property))
        entry.computedStyleDifferences.push({
          index: i,
          property,
          actual: a.getPropertyValue(property),
          expected: b.getPropertyValue(property),
        });
    }
    const ar = actual[i].getBoundingClientRect();
    const br = expected[i].getBoundingClientRect();
    if (Math.abs(ar.width - br.width) > 0.02 || Math.abs(ar.height - br.height) > 0.02)
      entry.layoutDifferences.push({
        index: i,
        actual: { width: ar.width, height: ar.height },
        expected: { width: br.width, height: br.height },
      });
  }
  if (entry.computedStyleDifferences.length || entry.layoutDifferences.length)
    throw new Error(`${entry.id}: computed style or layout differs from KaTeX`);
}

window.cellRect = (id) => {
  const rect = document.getElementById(id).getBoundingClientRect();
  return { x: rect.x, y: rect.y, width: rect.width, height: rect.height };
};
window.comparisonValidation = validation;
window.__ready = true;
