import fs from 'node:fs/promises';

for (const file of ['dist/index.mjs', 'dist/index.d.ts', 'dist/runtime/dotnet.js', 'dist/runtime/LICENSE.dotnet.txt', 'dist/runtime/THIRD-PARTY-NOTICES.dotnet.txt', 'dist/build-info.json', 'images/comparison-algebra.png', 'images/comparison-calculus.png', 'images/comparison-structures.png', 'images/comparison-dark.png', 'LICENSE', 'licenses/KaTeX-FONTS-OFL.txt', 'THIRD-PARTY-NOTICES.md']) {
  await fs.access(new URL(`../${file}`, import.meta.url));
}
const info = JSON.parse(await fs.readFile(new URL('../dist/build-info.json', import.meta.url), 'utf8'));
if (info.configuration !== 'Release' || info.aot !== true) throw new Error('npm distribution requires a Release WASM AOT build. Run build-wasm.ps1 without -NoAot.');
const metadata = JSON.parse(await fs.readFile(new URL('../package.json', import.meta.url), 'utf8'));
if (info.packageVersion !== metadata.version) throw new Error('Build metadata and npm package version disagree. Rebuild or stage the package first.');
console.error('Release AOT assets and separate code/font licenses are present.');
