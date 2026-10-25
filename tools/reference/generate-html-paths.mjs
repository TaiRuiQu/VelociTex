// Generates the C++ table of KaTeX's SVG path data.
//
// The HTML renderer has to emit byte-identical SVG markup, so it needs
// KaTeX's original path strings.  The layout data generator
// (generate-layout-data.mjs) normalises the paths into absolute drawing
// segments, which cannot be turned back into KaTeX's exact output, so this
// generator keeps the original strings.
//
// Usage: node tools/reference/generate-html-paths.mjs
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import ts from 'typescript';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'tools/reference/data/katex-0.19.0.files.json'), 'utf8'));
const sourceRoot = manifest.sourceRoot ?? path.join(root, 'artifacts/upstream/source');

function source(relative) {
  const bytes = fs.readFileSync(path.join(sourceRoot, relative));
  const expected = manifest.sourceFiles.find((x) => x.path === relative)?.sha256;
  const actual = crypto.createHash('sha256').update(bytes).digest('hex');
  if (expected && expected !== actual) {
    throw new Error(`Upstream source mismatch for ${relative}`);
  }
  return bytes.toString('utf8');
}

const geometrySource = source('src/svgGeometry.ts');
const js = ts.transpileModule(geometrySource, {
  compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 },
}).outputText;
const context = { exports: {} };
vm.runInNewContext(js, context, { timeout: 3000 });

const paths = context.exports.path;
const names = Object.keys(paths).sort();

function escapeCpp(value) {
  let out = '';
  for (const ch of value) {
    const code = ch.codePointAt(0);
    if (ch === '\\') out += '\\\\';
    else if (ch === '"') out += '\\"';
    else if (ch === '\n') out += '\\n';
    else if (ch === '\r') out += '\\r';
    else if (ch === '\t') out += '\\t';
    else if (code < 0x20 || code === 0x7f) out += `\\x${code.toString(16).padStart(2, '0')}`;
    else out += ch;
  }
  return out;
}

const lines = [];
lines.push('// VelociTex SVG path data.');
lines.push('//');
lines.push('// Generated from KaTeX 0.19.0 `src/svgGeometry.ts` by');
lines.push('// tools/reference/generate-html-paths.mjs.  The strings are the original');
lines.push('// KaTeX path data so that the generated SVG matches byte for byte.');
lines.push(
  `// svgGeometry.ts SHA-256: ${crypto.createHash('sha256').update(Buffer.from(geometrySource)).digest('hex')}`
);
lines.push('');
lines.push('#include "html_paths.hpp"');
lines.push('');
lines.push('namespace velocitex::generated {');
lines.push('');
lines.push('const HtmlPathData::Path HtmlPathData::Paths[] = {');
for (const name of names) {
  lines.push(`    {u"${name}", "${escapeCpp(paths[name])}"},`);
}
lines.push('};');
lines.push('');
lines.push('const int HtmlPathData::PathCount =');
lines.push('    static_cast<int>(sizeof(HtmlPathData::Paths) / sizeof(HtmlPathData::Paths[0]));');
lines.push('');
lines.push('const std::string_view* HtmlPathData::Find(std::u16string_view name) {');
lines.push('    for (const Path& path : Paths) {');
lines.push('        if (path.Name == name) {');
lines.push('            return &path.Data;');
lines.push('        }');
lines.push('    }');
lines.push('    return nullptr;');
lines.push('}');
lines.push('');
lines.push('}  // namespace velocitex::generated');
lines.push('');

const target = path.join(root, 'native/core/generated/html_paths.cpp');
fs.writeFileSync(target, lines.join('\n'));
console.log(`wrote ${target} (${names.length} paths)`);
