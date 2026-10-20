let module;
let inputPointer = 0;
let inputCapacity = 0;
let outputPointer = 0;
let outputCapacity = 0;

/** Load the standalone C++ MathML fast path. Returns false when unavailable. */
export async function initFastMath(url) {
  if (module) return true;
  try {
    const { default: createFastMath } = await import(/* @vite-ignore */ url);
    module = await createFastMath({
      locateFile: (file) => new URL(file, url).href,
      printErr: () => {},
    });
    return !!module._vt_mathml2;
  } catch {
    module = undefined;
    return false;
  }
}

export function fastMathAvailable() {
  return !!module;
}

/** Returns presentation MathML, or null when the fast parser does not support the input. */
export function renderFastMath(source, displayMode = false) {
  if (!module) return null;
  const needed = module.lengthBytesUTF8(source) + 1;
  if (needed > inputCapacity) {
    if (inputPointer) module._free(inputPointer);
    inputCapacity = Math.max(4096, needed * 2);
    inputPointer = module._malloc(inputCapacity);
    if (!inputPointer) return null;
  }
  module.stringToUTF8(source, inputPointer, inputCapacity);
  if (!outputPointer) {
    outputCapacity = 65536;
    outputPointer = module._malloc(outputCapacity);
    if (!outputPointer) return null;
  }
  let size = module._vt_mathml2(inputPointer, needed - 1, outputPointer, outputCapacity, displayMode ? 1 : 0);
  if (size > outputCapacity) {
    module._free(outputPointer);
    outputCapacity = Math.max(size, outputCapacity * 2);
    outputPointer = module._malloc(outputCapacity);
    if (!outputPointer) return null;
    size = module._vt_mathml2(inputPointer, needed - 1, outputPointer, outputCapacity, displayMode ? 1 : 0);
  }
  if (size < 0) return null;
  return module.UTF8ToString(outputPointer, size);
}
