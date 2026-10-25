// Standalone KaTeX-compatible HTML/CSS WebAssembly renderer.
// The module has no JavaScript imports; this adapter allocates a UTF-8 input
// buffer, calls the exported C ABI, and copies the result back to a string.

let module;
let initialization;
let decoder;
let stylesheetUrl;

const textDecoder = () => (decoder ??= new TextDecoder());

// The C++ core module has no JavaScript imports of its own.  Emscripten only
// asks the host to observe a memory growth, which this adapter does not need.
// Some Emscripten builds additionally link the WASI stubs even for a web
// target; the core never calls them, so empty implementations are enough.
const coreImports = {
  env: {
    emscripten_notify_memory_growth: () => {},
  },
  wasi_snapshot_preview1: {
    environ_sizes_get: () => 0,
    environ_get: () => 0,
    fd_close: () => 0,
    fd_write: () => 0,
    fd_seek: () => 0,
    proc_exit: (code) => {
      throw new Error(`VelociTex core exited with code ${code}.`);
    },
  },
};

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
        const { instance } = await WebAssembly.instantiate(bytes, coreImports);
        if (!instance.exports.vt_render || !instance.exports.vt_alloc) return false;
        // Emscripten emits the module as a "reactor": its static
        // initializers live behind the WASI `_initialize` entry point and the
        // JavaScript glue that would normally call it is not part of the
        // package.  Without the call the C++ tables that are built at load
        // time (the stretchy SVG images, for instance) stay empty.
        if (typeof instance.exports._initialize === 'function') instance.exports._initialize();
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

// Writes the UTF-8 of a text into the view and returns the number of the
// bytes that were written.
//
// The encoder of the platform is a call into the browser, and every formula
// goes through it.  The sources are short, so a loop over the characters is
// cheaper than the call.  The encoding is the one of the `TextEncoder`: a
// surrogate pair becomes one code point, and a lone surrogate becomes the
// replacement character.
function encodeUtf8(view, value) {
  let written = 0;
  for (let i = 0; i < value.length; i++) {
    let code = value.charCodeAt(i);
    if (code < 0x80) {
      view[written++] = code;
    } else if (code < 0x800) {
      view[written++] = 0xc0 | (code >> 6);
      view[written++] = 0x80 | (code & 0x3f);
    } else if (code >= 0xd800 && code <= 0xdbff && i + 1 < value.length) {
      const low = value.charCodeAt(i + 1);
      if (low >= 0xdc00 && low <= 0xdfff) {
        code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
        i++;
        view[written++] = 0xf0 | (code >> 18);
        view[written++] = 0x80 | ((code >> 12) & 0x3f);
        view[written++] = 0x80 | ((code >> 6) & 0x3f);
        view[written++] = 0x80 | (code & 0x3f);
        continue;
      }
      // A lone surrogate: the replacement character of the encoder.
      view[written++] = 0xef;
      view[written++] = 0xbf;
      view[written++] = 0xbd;
    } else if (code >= 0xd800 && code <= 0xdfff) {
      // A lone surrogate: the replacement character of the encoder.
      view[written++] = 0xef;
      view[written++] = 0xbf;
      view[written++] = 0xbd;
    } else {
      view[written++] = 0xe0 | (code >> 12);
      view[written++] = 0x80 | ((code >> 6) & 0x3f);
      view[written++] = 0x80 | (code & 0x3f);
    }
  }
  return written;
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
    inputView = null;
  } else {
    optionsPointer = pointer;
    optionsCapacity = size;
    optionsView = null;
  }
  return pointer;
}

// The views over the module memory are cached.  Creating a typed array view
// costs an allocation, and the memory only moves when it grows, which
// replaces the buffer and invalidates the views.
let cachedBuffer = null;
let inputView = null;
let optionsView = null;
let memoryView = null;

function views() {
  const buffer = module.memory.buffer;
  if (buffer !== cachedBuffer) {
    cachedBuffer = buffer;
    inputView = null;
    optionsView = null;
    memoryView = new Uint8Array(buffer);
  }
  return memoryView;
}

function write(which, value) {
  // Every UTF-16 code unit needs at most three UTF-8 bytes.
  const capacity = value.length * 3 + 3;
  const pointer = reserve(which, capacity);
  if (!pointer) return { pointer: 0, length: 0 };
  views();
  let view = which === 'input' ? inputView : optionsView;
  if (!view) {
    view = new Uint8Array(module.memory.buffer, pointer, which === 'input' ? inputCapacity : optionsCapacity);
    if (which === 'input') inputView = view;
    else optionsView = view;
  }
  const written = encodeUtf8(view, value);
  return { pointer, length: written };
}

// The options of a document travel with every formula and are usually the
// same, so the encoded form is kept and reused while the options do not
// change.  Encoding them again for every formula costs more than the parse
// of the options in the module.
let cachedOptionsJson = null;
let cachedOptionsLength = 0;

function writeOptions(optionsJson) {
  if (optionsJson === cachedOptionsJson) {
    return { pointer: optionsPointer, length: cachedOptionsLength };
  }
  const options = write('options', optionsJson);
  if (options.pointer) {
    cachedOptionsJson = optionsJson;
    cachedOptionsLength = options.length;
  }
  return options;
}

/**
 * Render using output 0=htmlAndMathml, 1=html, 2=mathml.
 *
 * Returns null when the module is unavailable, otherwise an object with the
 * payload status (`O` for a rendered document, `E` for a diagnostic) and the
 * payload body.  The status byte is read from the module memory instead of
 * being part of the decoded string, so the body never has to be sliced a
 * second time.  Returns null when unavailable.
 */
export function renderKatexMath(source, optionsJson, output) {
  if (!module) return null;
  const input = write('input', source);
  if (!input.pointer) return null;
  const options = writeOptions(optionsJson);
  if (!options.pointer) return null;
  const pointer = module.vt_render(input.pointer, input.length, options.pointer, options.length, output);
  if (!pointer) return null;
  try {
    const length = module.vt_result_len();
    // Decoding straight from the module memory avoids copying the payload
    // into an intermediate buffer first.  The status byte is read from the
    // module memory as well, so the body never has to be sliced a second time.
    const memory = views();
    const head = memory[pointer];
    const status = head === 0x4f ? 'O' : 'E';
    const body = memory.subarray(pointer + 1, pointer + length);
    return { status, text: textDecoder().decode(body) };
  } finally {
    module.vt_free_result(pointer);
  }
}
