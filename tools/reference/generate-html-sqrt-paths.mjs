// Generates the C++ port of KaTeX's square-root path templates.
//
// The templates in KaTeX's `src/svgGeometry.ts` are JavaScript template
// literals with a handful of interpolations.  This generator rewrites them
// into C++ string concatenation so that the generated SVG path matches KaTeX
// byte for byte.
//
// Usage: node tools/reference/generate-html-sqrt-paths.mjs
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'tools/reference/data/katex-0.19.0.files.json'), 'utf8'));
const sourceRoot = manifest.sourceRoot ?? path.join(root, 'artifacts/upstream/source');

const relative = 'src/svgGeometry.ts';
const bytes = fs.readFileSync(path.join(sourceRoot, relative));
const expected = manifest.sourceFiles.find((x) => x.path === relative)?.sha256;
const actual = crypto.createHash('sha256').update(bytes).digest('hex');
if (expected && expected !== actual) {
  throw new Error(`Upstream source mismatch for ${relative}`);
}
const source = bytes.toString('utf8');

const identifierMap = {
  extraViniculum: 'extra_viniculum',
  extraVinculum: 'extra_viniculum',
  hLinePad: 'h_line_pad',
  viewBoxHeight: 'view_box_height',
  vertSegment: 'vert_segment',
};

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

function translateExpression(expression) {
  return expression.replace(/[A-Za-z_][A-Za-z0-9_]*/g, (word) => identifierMap[word] ?? word);
}

// Extracts `const name = function(args) { ... return `template`; };`
const pattern = /const (sqrt[A-Za-z0-9]*)\s*=\s*function\s*\(([^)]*)\)\s*:\s*string\s*\{([\s\S]*?)\n\};/g;

const functions = [];
let match;
while ((match = pattern.exec(source)) !== null) {
  const [, name, args, body] = match;
  const template = /return\s*`([\s\S]*?)`;/.exec(body);
  if (!template) continue;
  functions.push({ name, args, body, template: template[1] });
}

if (functions.length < 5) {
  throw new Error(`Expected the square root path templates, found ${functions.length}.`);
}

const lines = [];
lines.push('// VelociTex square root path templates.');
lines.push('//');
lines.push('// Generated from KaTeX 0.19.0 `src/svgGeometry.ts` by');
lines.push('// tools/reference/generate-html-sqrt-paths.mjs.');
lines.push(`// svgGeometry.ts SHA-256: ${actual}`);
lines.push('');
lines.push('#include "html_sqrt_paths.hpp"');
lines.push('');
lines.push('#include <cstdio>');
lines.push('#include <cstdlib>');
lines.push('#include <string>');
lines.push('');
lines.push('namespace velocitex::generated {');
lines.push('namespace {');
lines.push('');
lines.push('// The JavaScript number to string conversion used by the template');
lines.push('// literals (shortest round-trip representation).');
lines.push('std::string Number(double value) {');
lines.push('    if (value == 0.0) {');
lines.push('        return "0";');
lines.push('    }');
lines.push('    if (value == static_cast<double>(static_cast<long long>(value)) &&');
lines.push('        value < 1e21 && value > -1e21) {');
lines.push('        return std::to_string(static_cast<long long>(value));');
lines.push('    }');
lines.push('    char buffer[64];');
lines.push('    const int written = std::snprintf(buffer, sizeof(buffer), "%.17g", value);');
lines.push('    for (int precision = 1; precision < 17; ++precision) {');
lines.push('        char candidate[64];');
lines.push('        const int count = std::snprintf(candidate, sizeof(candidate), "%.*g", precision, value);');
lines.push('        if (std::strtod(candidate, nullptr) == value) {');
lines.push('            return std::string(candidate, static_cast<size_t>(count));');
lines.push('        }');
lines.push('    }');
lines.push('    return std::string(buffer, static_cast<size_t>(written));');
lines.push('}');
lines.push('');
lines.push('}  // namespace');

for (const fn of functions) {
  const args = fn.args
    .split(',')
    .map((arg) => arg.trim().split(':')[0].trim())
    .map((arg) => `double ${identifierMap[arg] ?? arg}`);
  const signature = args.join(', ');
  lines.push('');
  lines.push(`std::string ${fn.name}(${signature}) {`);
  if (fn.name === 'sqrtTall') {
    lines.push('    // One path edge has a variable length: it runs vertically from the');
    lines.push('    // vinculum to a point near the bottom of the surd.');
    lines.push('    const double vert_segment = view_box_height - 54 - h_line_pad - extra_viniculum;');
    lines.push('    std::string out;');
  } else {
    lines.push('    std::string out;');
  }
  const parts = fn.template.split(/\$\{([^}]*)\}/);
  for (let i = 0; i < parts.length; i++) {
    if (i % 2 === 0) {
      if (parts[i].length > 0) {
        lines.push(`    out += "${escapeCpp(parts[i])}";`);
      }
    } else {
      lines.push(`    out += Number(${translateExpression(parts[i].trim())});`);
    }
  }
  lines.push('    return out;');
  lines.push('}');
}

lines.push('');
lines.push('}  // namespace velocitex::generated');
lines.push('');

const target = path.join(root, 'native/core/generated/html_sqrt_paths.cpp');
fs.writeFileSync(target, lines.join('\n'));
console.log(`wrote ${target} (${functions.map((f) => f.name).join(', ')})`);
