import * as vt from '/vt/index.mjs';

const formulas = await (await fetch('/comparison.json')).json();
const grid = document.getElementById('grid');
const cells = new Map();

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
    strict: 'ignore',
  });
}
await document.fonts.ready;
await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));

window.cellRect = (id) => {
  const rect = document.getElementById(id).getBoundingClientRect();
  return { x: rect.x, y: rect.y, width: rect.width, height: rect.height };
};
window.__ready = true;
