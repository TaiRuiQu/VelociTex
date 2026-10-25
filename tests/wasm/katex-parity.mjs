// Byte-for-byte parity between the standalone WASM HTML renderer and KaTeX
// 0.19.0. Run after `native/build-wasm-katex.ps1`. This test is intentionally
// browser-free so it can run in CI.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import katex from '../../tools/reference/node_modules/katex/dist/katex.mjs';

const root = path.resolve(fileURLToPath(new URL('../../', import.meta.url)));
const wasmPath = [
  path.join(root, 'artifacts/wasm/core/velocitex-core.wasm'),
  path.join(root, 'npm/velocitex/dist/runtime/velocitex-core.wasm'),
].find((file) => fs.existsSync(file));
if (!wasmPath) throw new Error('VelociTex core WASM is missing; run native/build-wasm-katex.ps1.');
const { instance } = await WebAssembly.instantiate(fs.readFileSync(wasmPath), {
  env: { emscripten_notify_memory_growth: () => {} },
  wasi_snapshot_preview1: {
    proc_exit: (code) => {
      throw new Error(`VelociTex core exited with code ${code}.`);
    },
  },
});
// Emscripten emits the module as a "reactor": the static initializers run
// from the WASI `_initialize` entry point, which the standalone loader has to
// call because no JavaScript glue ships with the module.
if (typeof instance.exports._initialize === 'function') instance.exports._initialize();
const wasm = instance.exports;
const encoder = new TextEncoder();
const decoder = new TextDecoder();

function render(source, options, output = 0) {
  const input = encoder.encode(source);
  const optionsBytes = encoder.encode(JSON.stringify(options));
  const inputPointer = wasm.vt_alloc(Math.max(1, input.length));
  const optionsPointer = wasm.vt_alloc(Math.max(1, optionsBytes.length));
  new Uint8Array(wasm.memory.buffer).set(input, inputPointer);
  new Uint8Array(wasm.memory.buffer).set(optionsBytes, optionsPointer);
  let pointer = 0;
  try {
    pointer = wasm.vt_render(inputPointer, input.length, optionsPointer, optionsBytes.length, output);
    assert(pointer, 'vt_render returned a null result');
    const length = wasm.vt_result_len();
    return decoder.decode(new Uint8Array(wasm.memory.buffer).slice(pointer, pointer + length));
  } finally {
    wasm.vt_free(inputPointer, Math.max(1, input.length));
    wasm.vt_free(optionsPointer, Math.max(1, optionsBytes.length));
    if (pointer) wasm.vt_free_result(pointer);
  }
}

function mapOptions(fixture) {
  const options = {};
  if (fixture.displayMode != null) options.displayMode = fixture.displayMode;
  if (fixture.macros) options.macros = fixture.macros;
  if (fixture.options) {
    for (const [key, value] of Object.entries(fixture.options)) {
      if (key === 'strict') options.strict = value === true ? 'error' : value === false ? 'ignore' : value;
      else if (['maxExpand', 'maxSize', 'minRuleThickness', 'colorIsTextColor', 'leqno', 'fleqn'].includes(key))
        options[key] = value;
    }
  }
  if (options.strict == null) options.strict = 'warn';
  return options;
}

const corpus = JSON.parse(fs.readFileSync(path.join(root, 'tests/fixtures/core.json'), 'utf8'));
let valid = 0;
let invalid = 0;
let mathml = 0;
let skipped = 0;
let diagnosticRanges = 0;
let diagnosticRangeDifferences = 0;
let softErrorDifferences = 0;
const failures = [];
for (const fixture of corpus.cases) {
  const options = mapOptions(fixture);
  if (fixture.expectation === 'invalid') {
    let expectedError;
    try {
      katex.renderToString(fixture.latex, options);
    } catch (error) {
      expectedError = error;
    }
    if (expectedError?.name !== 'ParseError') {
      skipped++;
      continue;
    }
    const actual = render(fixture.latex, { ...options, throwOnError: true });
    if (actual[0] !== 'E') failures.push(`${fixture.id}: expected a diagnostic`);
    else {
      const diagnostic = JSON.parse(actual.slice(1));
      if (expectedError.position !== undefined) {
        diagnosticRanges++;
        if (diagnostic.start !== expectedError.position || diagnostic.length !== expectedError.length)
          diagnosticRangeDifferences++;
      }
    }
    try {
      const expectedSoft = katex.renderToString(fixture.latex, { ...options, throwOnError: false });
      const soft = render(fixture.latex, { ...options, throwOnError: false });
      if (soft[0] === 'E') {
        // The module reports the failure and carries KaTeX's error markup.
        const diagnostic = JSON.parse(soft.slice(1));
        if (!diagnostic.html || !diagnostic.html.startsWith('<span'))
          failures.push(`${fixture.id}: soft error markup is missing`);
        else if (diagnostic.html !== expectedSoft) softErrorDifferences++;
      } else if (soft[0] === 'O') {
        // The core rendered KaTeX's error markup itself.
        if (soft.slice(1) !== expectedSoft) softErrorDifferences++;
      } else {
        failures.push(`${fixture.id}: soft error did not return a diagnostic`);
      }
    } catch {
      /* KaTeX itself cannot produce readable output for this fixture. */
    }
    invalid++;
    continue;
  }

  let expected;
  try {
    expected = katex.renderToString(fixture.latex, options);
  } catch (error) {
    if (error?.name !== 'ParseError') {
      skipped++;
      continue;
    }
    failures.push(`${fixture.id}: KaTeX rejected a valid fixture`);
    continue;
  }
  const actual = render(fixture.latex, { ...options, throwOnError: true });
  if (actual[0] !== 'O') failures.push(`${fixture.id}: WASM reported an error`);
  else if (actual.slice(1) !== expected) failures.push(`${fixture.id}: HTML differs`);
  const expectedMathml = katex.renderToString(fixture.latex, { ...options, output: 'mathml' });
  const actualMathml = render(fixture.latex, { ...options, throwOnError: true }, 2);
  if (actualMathml[0] !== 'O') failures.push(`${fixture.id}: WASM MathML reported an error`);
  else if (actualMathml.slice(1) !== expectedMathml) failures.push(`${fixture.id}: MathML differs`);
  const expectedHtmlOnly = katex.renderToString(fixture.latex, { ...options, output: 'html' });
  const actualHtmlOnly = render(fixture.latex, { ...options, throwOnError: true }, 1);
  if (actualHtmlOnly[0] !== 'O') failures.push(`${fixture.id}: WASM HTML-only reported an error`);
  else if (actualHtmlOnly.slice(1) !== expectedHtmlOnly) failures.push(`${fixture.id}: HTML-only differs`);
  valid++;
  mathml++;
  if (failures.length > 40) break;
}
assert.deepEqual(failures, []);
console.log(
  `KaTeX HTML parity passed: ${valid} valid HTML+MathML documents are byte-identical (plus HTML-only), ${invalid} invalid formulas return diagnostics ` +
    `(${diagnosticRanges - diagnosticRangeDifferences}/${diagnosticRanges} source ranges and ` +
    `${invalid - softErrorDifferences}/${invalid} readable error documents match KaTeX), ${mathml} MathML documents, ` +
    `${skipped} skipped, ${corpus.cases.length} fixtures.`
);
