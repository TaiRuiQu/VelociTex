import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';

const output = fileURLToPath(new URL('../artifacts/tests/', import.meta.url));
const jit = JSON.parse(await fs.readFile(path.join(output, 'jit/report.json'), 'utf8'));
const aot = JSON.parse(await fs.readFile(path.join(output, 'aot/report.json'), 'utf8'));
assert.equal(jit.nativeAot, false);
assert.equal(aot.nativeAot, true);
assert.deepEqual(jit.results, aot.results, 'JIT/AOT semantics, diagnostics and geometry');
const exports = [];
for (const result of jit.results.filter((x) => x.success)) {
  for (const name of ['formula.png', 'formula.svg', 'formula.mathml']) {
    const relative = path.join(result.id, result.scenario, name);
    const j = await fs.readFile(path.join(output, 'jit', relative));
    const a = await fs.readFile(path.join(output, 'aot', relative));
    assert.deepEqual(j, a, `Runtime output differs: ${relative}`);
    exports.push({ path: relative.replaceAll('\\', '/'), sha256: createHash('sha256').update(a).digest('hex') });
  }
}
await fs.writeFile(
  path.join(output, 'runtime-parity.json'),
  JSON.stringify({ identicalExports: exports.length, exports }, null, 2)
);
console.log(`PASS: JIT/AOT results and ${exports.length} exports match.`);
