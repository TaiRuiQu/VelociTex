// Stages the browser package that the acceptance tests serve.
//
// The full distribution is produced by `build-wasm.ps1`, which also publishes
// the .NET runtime for the legacy SVG contract.  The HTML/MathML fast path
// only needs the C++ core module, the KaTeX stylesheet and the fonts, so this
// script stages exactly those into a directory the browser tests can serve.
//
// Usage: node tests/wasm/stage-core-dist.mjs [target] [wasm]
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(fileURLToPath(new URL('../../', import.meta.url)));
const target = path.resolve(process.argv[2] ?? path.join(root, 'npm/velocitex/dist'));
const wasm = path.resolve(process.argv[3] ?? path.join(root, 'artifacts/wasm/core/velocitex-core.wasm'));
const packageRoot = path.join(root, 'npm/velocitex');

await fs.access(wasm);
await fs.mkdir(path.join(target, 'runtime'), { recursive: true });
for (const file of ['index.mjs', 'index.d.ts', 'katexmath.mjs', 'fastmath.mjs']) {
  await fs.copyFile(path.join(packageRoot, 'src', file), path.join(target, file));
}
await fs.copyFile(wasm, path.join(target, 'runtime', 'velocitex-core.wasm'));
await fs.copyFile(path.join(packageRoot, 'assets', 'katex.min.css'), path.join(target, 'runtime', 'katex.min.css'));
await fs.copyFile(path.join(packageRoot, 'assets', 'katex.min.css'), path.join(target, 'katex.min.css'));
await fs.cp(path.join(packageRoot, 'assets', 'fonts'), path.join(target, 'runtime', 'fonts'), {
  recursive: true,
});
await fs.cp(path.join(packageRoot, 'assets', 'fonts'), path.join(target, 'fonts'), { recursive: true });
console.log(`Staged the C++ core browser package at ${target}`);
