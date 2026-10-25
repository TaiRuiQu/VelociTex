import { initKatexMath, renderKatexMath, katexStylesheetUrl } from './katexmath.mjs';

const defaultRuntimeUrl = new URL('./runtime/', import.meta.url);
let runtimeUrl = defaultRuntimeUrl;
let initialization;
let compiler;
let compilerSvg;
let compilerMathml;
let compilerSvgSimple;
let compilerMathmlSimple;
let compilerSvgSharedSimple;
let serial = 0;
let fastInitialization;
let fastReady = false;
let injectStyles = true;

async function ensureFast() {
  if (!fastInitialization) {
    fastInitialization = (async () => {
      fastReady = await initKatexMath(new URL('velocitex-core.wasm', runtimeUrl).href, { injectStyles });
      return fastReady;
    })();
  }
  await fastInitialization;
  return fastReady;
}
const instanceId = `vt${Math.random().toString(36).slice(2)}_`;
const hidden =
  'position:absolute;width:1px;height:1px;padding:0;border:0;overflow:hidden;clip:rect(0,0,0,0);white-space:nowrap';
const svgNamespace = 'http://www.w3.org/2000/svg';
let definitionsHost;
const installedDefinitions = new Set();

function installDefinitions(definitions) {
  if (!definitions) return;
  if (!definitionsHost) {
    if (typeof globalThis.document === 'undefined') return;
    definitionsHost = document.createElementNS(svgNamespace, 'svg');
    definitionsHost.setAttribute('aria-hidden', 'true');
    definitionsHost.setAttribute('style', hidden);
    definitionsHost.append(document.createElementNS(svgNamespace, 'defs'));
    document.body.append(definitionsHost);
  }
  const target = definitionsHost.firstChild;
  for (const chunk of definitions.split('<path ')) {
    if (!chunk) continue;
    const id = /^id="([^"]+)"/.exec(chunk)?.[1];
    if (id && installedDefinitions.has(id)) continue;
    target.insertAdjacentHTML('beforeend', '<path ' + chunk);
    if (id) installedDefinitions.add(id);
  }
}

/** Initialize once; render functions also initialize automatically. */
export function init(options = {}) {
  const requested =
    options.assetBaseUrl == null
      ? runtimeUrl
      : new URL(options.assetBaseUrl, globalThis.location?.href ?? import.meta.url);
  if (!requested.pathname.endsWith('/')) requested.pathname += '/';
  if (initialization && requested.href !== runtimeUrl.href)
    throw new Error('VelociTex is already initializing with a different assetBaseUrl.');
  if (!initialization) {
    runtimeUrl = requested;
    injectStyles = options.injectStyles !== false;
    // The KaTeX-compatible engine is the default path. Load it eagerly, but
    // defer the much larger .NET runtime until the SVG/legacy contract is used.
    initialization = ensureFast().then(() => undefined);
  }
  return initialization;
}

let dotnetInitialization;
async function ensureDotnet() {
  if (!dotnetInitialization) {
    dotnetInitialization = (async () => {
      if (typeof globalThis.document === 'undefined')
        throw new Error('VelociTex WASM requires a browser. renderToString is not a Node.js SSR API.');
      const { dotnet } = await import(/* @vite-ignore */ new URL('dotnet.js', runtimeUrl).href);
      const runtime = await dotnet.withDiagnosticTracing(false).create();
      const exports = await runtime.getAssemblyExports(runtime.getConfig().mainAssemblyName);
      compiler = exports.VelociTex.Wasm.BrowserBindings.Compile;
      compilerSvg = exports.VelociTex.Wasm.BrowserBindings.CompileSvg;
      compilerMathml = exports.VelociTex.Wasm.BrowserBindings.CompileMathMl;
      compilerSvgSimple = exports.VelociTex.Wasm.BrowserBindings.CompileSvgSimple;
      compilerMathmlSimple = exports.VelociTex.Wasm.BrowserBindings.CompileMathMlSimple;
      compilerSvgSharedSimple = exports.VelociTex.Wasm.BrowserBindings.CompileSharedSimple;
    })().catch((error) => {
      dotnetInitialization = undefined;
      throw new Error(
        `Unable to initialize VelociTex WASM at ${runtimeUrl.href}. Serve the complete runtime directory over HTTP(S). ${error.message}`,
        { cause: error }
      );
    });
  }
  return dotnetInitialization;
}

export class ParseError extends Error {
  constructor(source, diagnostic) {
    super(`VelociTex ${diagnostic.code}: ${diagnostic.message}`);
    this.name = 'ParseError';
    this.source = source;
    this.code = diagnostic.code;
    this.range = { start: diagnostic.start, length: diagnostic.length };
  }
}

function escapeHtml(value) {
  return value.replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
}

// The option record is serialized straight into JSON.  Every formula goes
// through this function, so the intermediate object and the generic
// stringifier are worth avoiding.  The quoted key names are precomputed
// because they never change.
const booleanOptions = [
  ['displayMode', '"displayMode"'],
  ['colorIsTextColor', '"colorIsTextColor"'],
  ['leqno', '"leqno"'],
  ['fleqn', '"fleqn"'],
];
const numberOptions = [
  ['fontSize', '"fontSize"'],
  ['maxInputLength', '"maxInputLength"'],
  ['maxNodes', '"maxNodes"'],
  ['maxNesting', '"maxNesting"'],
  ['maxTokens', '"maxTokens"'],
  ['maxExpand', '"maxExpand"'],
  ['maxSize', '"maxSize"'],
  ['minRuleThickness', '"minRuleThickness"'],
  ['displayWidth', '"displayWidth"'],
];
const strictValues = new Map([
  [true, '"error"'],
  [false, '"ignore"'],
  ['ignore', '"ignore"'],
  ['warn', '"warn"'],
  ['error', '"error"'],
]);
const outputValues = new Map([
  ['html', '"html"'],
  ['svg', '"svg"'],
  ['mathml', '"mathml"'],
]);

function compileOptions(options) {
  // The JSON is built in place.  Every formula goes through this function, so
  // the intermediate array and the join are worth avoiding.
  let json = '{';
  let separator = '';
  for (let i = 0; i < booleanOptions.length; i++) {
    const key = booleanOptions[i][0];
    const value = options[key];
    if (value == null) continue;
    if (typeof value !== 'boolean') throw new TypeError(`${key} must be a boolean.`);
    json += separator + booleanOptions[i][1] + ':' + (value ? 'true' : 'false');
    separator = ',';
  }
  for (let i = 0; i < numberOptions.length; i++) {
    const key = numberOptions[i][0];
    const value = options[key];
    if (value == null) continue;
    if (key === 'maxSize' && value === Infinity) continue;
    if (typeof value !== 'number' || !Number.isFinite(value)) throw new TypeError(`${key} must be a finite number.`);
    if (key.startsWith('max') && key !== 'maxSize' && !Number.isInteger(value))
      throw new TypeError(`${key} must be an integer.`);
    json += separator + numberOptions[i][1] + ':' + JSON.stringify(value);
    separator = ',';
  }
  if (options.strict != null) {
    const strict = strictValues.get(options.strict);
    if (strict === undefined) throw new TypeError('strict must be ignore, warn, error, true or false.');
    json += separator + '"strict":' + strict;
    separator = ',';
  }
  if (options.macros != null) {
    if (typeof options.macros !== 'object' || Array.isArray(options.macros))
      throw new TypeError('macros must be a record of string definitions.');
    let macros = '';
    let macroSeparator = '';
    for (const name of Object.keys(options.macros)) {
      const definition = options.macros[name];
      if (typeof definition !== 'string') throw new TypeError(`Macro ${name} must be a string.`);
      macros += macroSeparator + JSON.stringify(name) + ':' + JSON.stringify(definition);
      macroSeparator = ',';
    }
    json += separator + '"macros":{' + macros + '}';
    separator = ',';
  }
  if (options.output != null) {
    const output = outputValues.get(options.output);
    if (output === undefined) throw new TypeError('output must be html, svg or mathml.');
    json += separator + '"output":' + output;
    separator = ',';
  }
  json += separator + '"throwOnError":' + (options.throwOnError !== false ? 'true' : 'false');
  separator = ',';
  if (options.errorColor != null) {
    if (
      typeof options.errorColor !== 'string' ||
      !/^#(?:[0-9a-f]{3}|[0-9a-f]{4}|[0-9a-f]{6}|[0-9a-f]{8})$/i.test(options.errorColor)
    )
      throw new TypeError('errorColor must be a hexadecimal color.');
    json += separator + '"errorColor":' + JSON.stringify(options.errorColor);
  }
  return json + '}';
}

function canUseSimpleOptions(options) {
  for (const key of Object.keys(options)) {
    if (!['displayMode', 'output', 'throwOnError', 'trust', '__shared', 'fastMathml', 'fastHtml'].includes(key))
      return false;
  }
  return true;
}

function canUseFastOptions(options) {
  // The standalone engine has no host display width and does not enforce the
  // legacy node/nesting/token budgets; keep the exact Core path for those.
  if (options.displayWidth != null) return false;
  if (options.maxNodes != null || options.maxNesting != null || options.maxTokens != null) return false;
  return true;
}

// The checks every request goes through before the engine is asked to render
// anything.  They are kept apart from the rendering itself, so that the hot
// path can run them without a promise in between.
function prepare(source, options) {
  if (typeof source !== 'string') throw new TypeError('Formula source must be a string.');
  const json = compileOptions(options);
  // Validate before starting a large download for a malformed request.
  const limit = options.maxInputLength ?? 32768;
  if (limit > 0 && limit <= 1048576 && source.length > limit) {
    throw new ParseError(source, {
      code: 'InputLimit',
      message: 'Input length budget exceeded.',
      start: limit,
      length: source.length - limit,
    });
  }
  return json;
}

// The standalone engine serves every request but the legacy SVG contract and
// the budgets of the full Core.
function canUseFast(options) {
  return (
    options.output !== 'svg' && options.fastMathml !== false && options.fastHtml !== false && canUseFastOptions(options)
  );
}

// Renders with the standalone engine.  Returns null when the module is not
// available, so that the caller can fall back to the .NET runtime.
function renderFast(source, options, json) {
  const output = options.output === 'mathml' ? 2 : options.output === 'html' ? 1 : 0;
  const raw = renderKatexMath(source, json, output);
  if (!raw) return null;
  if (raw.status === 'O')
    return options.output === 'mathml'
      ? { success: true, mathml: raw.text, __fast: true }
      : { success: true, html: raw.text, __fast: true };
  let diagnostic;
  try {
    diagnostic = JSON.parse(raw.text);
  } catch {
    diagnostic = { code: 'UnexpectedToken', message: 'Unable to render formula.', start: 0, length: source.length };
  }
  if (options.throwOnError === false)
    return {
      success: true,
      __fast: true,
      __soft: true,
      diagnostic,
      html: diagnostic.html ?? '',
      mathml: diagnostic.html ?? '',
    };
  throw new ParseError(source, diagnostic);
}

// The synchronous fast path.  It returns null when the standalone engine
// cannot serve the request; the caller then takes the asynchronous path.
// Nothing here builds a promise: the request goes into the module and the
// markup comes back in the same tick.
function compileFast(source, options, json) {
  if (!fastReady || !canUseFast(options)) return null;
  return renderFast(source, options, json);
}

// The asynchronous path: it waits for the module, then falls back to the
// .NET runtime for the contracts the standalone engine does not serve.
async function compileSlow(source, options, json) {
  if (
    canUseFast(options) &&
    // The module is loaded once; the wait is only needed while it is still
    // loading, so the hot path does not await a settled promise.
    (fastReady || (await ensureFast()))
  ) {
    const result = renderFast(source, options, json);
    if (result) return result;
  }
  await ensureDotnet();
  const prefix = instanceId + ++serial;
  if (options.__shared === true && canUseSimpleOptions(options)) {
    const raw = compilerSvgSharedSimple(source, !!options.displayMode, instanceId, prefix);
    if (raw[0] === 'E') throw new ParseError(source, JSON.parse(raw.slice(1)));
    const [definitions, svg, mathml, width, height, baseline, logicalWidth, logicalHeight] = raw
      .slice(1)
      .split('\u0001');
    return {
      success: true,
      svg,
      mathml,
      width: Number(width),
      height: Number(height),
      baseline: Number(baseline),
      logicalWidth: Number(logicalWidth),
      logicalHeight: Number(logicalHeight),
      __defs: definitions,
    };
  }
  if (options.output === 'svg' || options.output === 'mathml') {
    const simple = canUseSimpleOptions(options);
    const raw =
      options.output === 'svg'
        ? simple
          ? compilerSvgSimple(source, !!options.displayMode, prefix)
          : compilerSvg(source, json, prefix)
        : simple
          ? compilerMathmlSimple(source, !!options.displayMode, prefix)
          : compilerMathml(source, json, prefix);
    if (raw[0] === 'E') throw new ParseError(source, JSON.parse(raw.slice(1)));
    return options.output === 'svg' ? { success: true, svg: raw.slice(1) } : { success: true, mathml: raw.slice(1) };
  }
  const result = JSON.parse(compiler(source, json, prefix));
  if (!result.success) throw new ParseError(source, result);
  return result;
}

function markup(result, options) {
  if (result.__fast) return options.output === 'mathml' ? result.mathml : result.html;
  if (options.output === 'mathml') return result.mathml;
  if (options.output === 'svg') return result.svg;
  if (options.output != null && options.output !== 'html') throw new TypeError('output must be html, svg or mathml.');
  const svg = result.svg.replace('<svg ', '<svg aria-hidden="true" style="display:block" ');
  const formula = `<span class="velocitex" style="position:relative;display:inline-block;line-height:0;vertical-align:${result.baseline - result.height}px"><span style="${hidden}">${result.mathml}</span>${svg}</span>`;
  return options.displayMode
    ? `<span class="velocitex-display" style="display:block;text-align:center;margin:1em 0">${formula}</span>`
    : formula;
}

function errorMarkup(error, options) {
  const color = options.errorColor ?? '#cc0000';
  if (!/^#(?:[0-9a-f]{3}|[0-9a-f]{4}|[0-9a-f]{6}|[0-9a-f]{8})$/i.test(color))
    throw new TypeError('errorColor must be a hexadecimal color.');
  return `<span class="velocitex velocitex-error" style="color:${color}" title="${escapeHtml(error.message)}">${escapeHtml(error.source)}</span>`;
}

/** Return KaTeX-compatible HTML (with the bundled stylesheet), SVG or MathML. */
export async function renderToString(source, options = {}) {
  try {
    // The standalone engine is the hot path.  When it is loaded the request
    // is rendered without a promise in between: the markup is ready in the
    // first tick of the caller instead of the third.
    const json = prepare(source, options);
    const fast = compileFast(source, options, json);
    if (fast) return markup(fast, options);
    return markup(await compileSlow(source, options, json), options);
  } catch (error) {
    if (!(error instanceof ParseError) || options.throwOnError !== false) throw error;
    return errorMarkup(error, options);
  }
}

/** Replace one element with a complete formula. Original source remains available for copying. */
export async function render(source, element, options = {}) {
  if (!element || element.nodeType !== 1) throw new TypeError('render requires a target Element.');
  const settings = { ...options, __shared: true };
  let result;
  try {
    const json = prepare(source, settings);
    const fast = compileFast(source, settings, json);
    result = fast ?? (await compileSlow(source, settings, json));
  } catch (error) {
    if (!(error instanceof ParseError) || options.throwOnError !== false) throw error;
    element.innerHTML = errorMarkup(error, options);
    element.setAttribute('data-velocitex-rendered', '');
    element.setAttribute('data-velocitex-source', source);
    return;
  }
  installDefinitions(result.__defs);
  element.innerHTML = markup(result, options);
  element.setAttribute('data-velocitex-rendered', '');
  element.setAttribute('data-velocitex-source', source);
}

const defaultDelimiters = [
  { left: '$$', right: '$$', display: true },
  { left: '\\[', right: '\\]', display: true },
  { left: '\\(', right: '\\)', display: false },
];
const excluded =
  'script,style,pre,code,textarea,select,option,noscript,svg,math,[contenteditable]:not([contenteditable="false"]),[data-velocitex-rendered],.velocitex,.velocitex-ignore';
function escaped(text, index) {
  let count = 0;
  while (index > 0 && text[--index] === '\\') count++;
  return count % 2 === 1;
}

function splitMath(text, delimiters) {
  const parts = [];
  let copied = 0;
  for (let i = 0; i < text.length; i++) {
    if (escaped(text, i)) continue;
    const delimiter = delimiters.find((d) => text.startsWith(d.left, i));
    if (!delimiter) continue;
    const start = i + delimiter.left.length;
    let depth = 0,
      end = -1;
    for (let j = start; j < text.length; j++) {
      if (escaped(text, j)) continue;
      if (depth === 0 && text.startsWith(delimiter.right, j)) {
        end = j;
        break;
      }
      if (text[j] === '{') depth++;
      if (text[j] === '}') depth = Math.max(0, depth - 1);
    }
    if (end < 0) continue;
    if (i > copied) parts.push({ text: text.slice(copied, i) });
    parts.push({ source: text.slice(start, end), display: delimiter.display });
    copied = end + delimiter.right.length;
    i = copied - 1;
  }
  if (copied < text.length) parts.push({ text: text.slice(copied) });
  return parts;
}

/** Render [data-math] elements and delimited text; repeated calls skip rendered output. */
export async function renderAll(root = globalThis.document?.body, options = {}) {
  if (!root || ![1, 9, 11].includes(root.nodeType))
    throw new TypeError('renderAll requires an Element, Document or DocumentFragment.');
  const delimiters = (options.delimiters ?? defaultDelimiters).map((d) => ({ ...d }));
  if (
    delimiters.some(
      (d) =>
        typeof d.left !== 'string' ||
        !d.left ||
        typeof d.right !== 'string' ||
        !d.right ||
        typeof d.display !== 'boolean'
    )
  )
    throw new TypeError('Each delimiter requires nonempty left/right strings and a display boolean.');
  delimiters.sort((a, b) => b.left.length - a.left.length);
  const max = options.maxFormulas ?? 1000;
  if (!Number.isInteger(max) || max <= 0 || max > 10000)
    throw new TypeError('maxFormulas must be an integer between 1 and 10000.');
  const selector = options.selector ?? '[data-math]';
  const base = { throwOnError: false, ...options };
  const report = { rendered: 0, errors: [] };
  let count = 0;
  async function one(source, displayMode, element) {
    if (++count > max) throw new RangeError(`renderAll exceeded maxFormulas (${max}).`);
    const settings = { ...base, displayMode };
    try {
      const json = prepare(source, settings);
      const fast = compileFast(source, settings, json);
      const result = fast ?? (await compileSlow(source, settings, json));
      if (result.__soft) {
        report.errors.push({ source, error: new ParseError(source, result.diagnostic), element });
        return markup(result, settings);
      }
      report.rendered++;
      return markup(result, settings);
    } catch (error) {
      if (!(error instanceof ParseError) || settings.throwOnError) throw error;
      report.errors.push({ source, error, element });
      return errorMarkup(error, settings);
    }
  }
  const elements = [...root.querySelectorAll(selector)];
  if (root.nodeType === 1 && root.matches(selector)) elements.unshift(root);
  for (const element of elements) {
    if (!root.contains(element)) continue; // A selected ancestor may already have replaced this node.
    if (element.closest(excluded)) continue;
    const source = element.getAttribute('data-math') ?? element.textContent;
    const displayMode = element.hasAttribute('data-display-mode')
      ? element.getAttribute('data-display-mode') !== 'false'
      : !!base.displayMode;
    element.innerHTML = await one(source, displayMode, element);
    element.setAttribute('data-velocitex-rendered', '');
    element.setAttribute('data-velocitex-source', source);
  }
  const doc = root.ownerDocument ?? root;
  const walker = doc.createTreeWalker(root, 4, {
    acceptNode(node) {
      return node.parentElement?.closest(excluded) ? 2 : 1;
    },
  });
  const nodes = [];
  while (walker.nextNode()) nodes.push(walker.currentNode);
  for (const node of nodes) {
    const parts = splitMath(node.data, delimiters);
    if (!parts.some((p) => p.source !== undefined)) continue;
    const fragment = doc.createDocumentFragment();
    for (const part of parts) {
      if (part.source === undefined) {
        fragment.append(doc.createTextNode(part.text));
        continue;
      }
      const host = doc.createElement('span');
      host.innerHTML = await one(part.source, part.display, host);
      host.setAttribute('data-velocitex-rendered', '');
      host.setAttribute('data-velocitex-source', part.source);
      fragment.append(host);
    }
    node.replaceWith(fragment);
  }
  return report;
}

/** KaTeX-compatible stylesheet URL injected by the fast renderer, when available. */
export function stylesheetUrl() {
  return katexStylesheetUrl();
}

export default { init, render, renderToString, renderAll, ParseError, stylesheetUrl };
