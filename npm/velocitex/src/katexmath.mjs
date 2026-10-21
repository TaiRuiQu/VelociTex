// Standalone KaTeX-compatible HTML/CSS WebAssembly renderer.
// The module has no JavaScript imports; this adapter allocates a UTF-8 input
// buffer, calls the exported C ABI, and copies the result back to a string.

let module;
let initialization;
let encoder;
let decoder;
let stylesheetUrl;

const textEncoder = () => (encoder ??= new TextEncoder());
const textDecoder = () => (decoder ??= new TextDecoder());

async function injectStyles(url) {
  if (typeof document === 'undefined' || !document.head) return;
  const href = new URL('katex.min.css', url).href;
  stylesheetUrl = href;
  if ([...document.querySelectorAll('link[rel="stylesheet"]')].some((link) => link.href === href)) return;
  if (
    [...document.querySelectorAll('link[rel="stylesheet"]')].some((link) =>
      /(?:^|\/)katex(?:\.min)?\.css(?:$|\?)/.test(link.href)
    )
  )
    return;
  const link = document.createElement('link');
  link.rel = 'stylesheet';
  link.href = href;
  link.dataset.velocitex = 'katex';
  const loaded = new Promise((resolve) => {
    link.addEventListener('load', resolve, { once: true });
    link.addEventListener('error', resolve, { once: true });
  });
  document.head.append(link);
  await loaded;
}

/** Load and instantiate the renderer. Returns false when the asset is unavailable. */
export async function initKatexMath(url, options = {}) {
  if (module) {
    if (options.injectStyles !== false) await injectStyles(url);
    return true;
  }
  if (!initialization) {
    initialization = (async () => {
      try {
        const response = await fetch(url);
        if (!response.ok) return false;
        const bytes = await response.arrayBuffer();
        const { instance } = await WebAssembly.instantiate(bytes, {});
        if (!instance.exports.vt_render || !instance.exports.vt_alloc) return false;
        module = instance.exports;
        if (options.injectStyles !== false) await injectStyles(url);
        return true;
      } catch {
        module = undefined;
        return false;
      }
    })();
  }
  return initialization;
}

export function katexMathAvailable() {
  return !!module;
}

export function katexStylesheetUrl() {
  return stylesheetUrl;
}

let inputPointer = 0;
let inputCapacity = 0;
let optionsPointer = 0;
let optionsCapacity = 0;

function reserve(which, capacity) {
  const current = which === 'input' ? inputPointer : optionsPointer;
  const existing = which === 'input' ? inputCapacity : optionsCapacity;
  if (existing >= capacity) return current;
  let size = Math.max(1024, existing || 0);
  while (size < capacity) size *= 2;
  const pointer = module.vt_alloc(size);
  if (!pointer) return 0;
  if (current) module.vt_free(current, Math.max(1, existing));
  if (which === 'input') {
    inputPointer = pointer;
    inputCapacity = size;
  } else {
    optionsPointer = pointer;
    optionsCapacity = size;
  }
  return pointer;
}

function write(which, value) {
  // Every UTF-16 code unit needs at most three UTF-8 bytes.
  const capacity = value.length * 3 + 3;
  const pointer = reserve(which, capacity);
  if (!pointer) return { pointer: 0, length: 0 };
  const view = new Uint8Array(module.memory.buffer, pointer, which === 'input' ? inputCapacity : optionsCapacity);
  const { written } = textEncoder().encodeInto(value, view);
  return { pointer, length: written };
}

/** Render using output 0=htmlAndMathml, 1=html, 2=mathml. Returns null when unavailable. */
export function renderKatexMath(source, optionsJson, output) {
  if (!module) return null;
  const input = write('input', source);
  if (!input.pointer) return null;
  const options = write('options', optionsJson);
  if (!options.pointer) return null;
  const pointer = module.vt_render(input.pointer, input.length, options.pointer, options.length, output);
  if (!pointer) return null;
  try {
    const length = module.vt_result_len();
    return textDecoder().decode(new Uint8Array(module.memory.buffer).slice(pointer, pointer + length));
  } finally {
    module.vt_free_result(pointer);
  }
}
