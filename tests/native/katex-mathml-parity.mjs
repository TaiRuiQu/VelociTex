// Compares the C++ core's MathML output against KaTeX byte for byte.
//
// MathML is the second output format of KaTeX.  The browser acceptance test
// (`tests/wasm/katex-browser-parity.mjs`) compares the default
// `htmlAndMathml` output, so both halves have to match.
//
// Usage: node tests/native/katex-mathml-parity.mjs [--verbose] [--limit N]
//                                             [--corpus curated|realworld|workloads|all]
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../..');
const require = createRequire(path.join(root, 'tools/reference/'));
const katex = require('./node_modules/katex');

const verbose = process.argv.includes('--verbose');
const limitIndex = process.argv.indexOf('--limit');
const limit = limitIndex >= 0 ? Number(process.argv[limitIndex + 1]) : 6;
const corpusIndex = process.argv.indexOf('--corpus');
const corpusChoice = corpusIndex >= 0 ? process.argv[corpusIndex + 1] : 'all';

const buildDir = process.env.VELOCITEX_BUILD_DIR ?? path.join(os.tmpdir(), 'velocitex-native-parity');
const nativeDir = process.env.VELOCITEX_NATIVE_DIR ?? path.join(root, 'native');
const probe = path.join(buildDir, 'katex-mathml-probe');

function build() {
  fs.mkdirSync(buildDir, { recursive: true });
  const source = path.join(buildDir, 'mathml_probe.cpp');
  fs.writeFileSync(
    source,
    `
#include "api.h"
#include <cstdio>
#include <string>
#include <vector>

// Decodes UTF-8 into UTF-16 so that the probe can feed non-ASCII formulas to
// the core exactly like the JavaScript side does.
static std::vector<uint16_t> DecodeUtf8(const std::string& text) {
    std::vector<uint16_t> out;
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t code = 0;
        size_t extra = 0;
        if (c < 0x80) { code = c; extra = 0; }
        else if ((c & 0xE0) == 0xC0) { code = c & 0x1Fu; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { code = c & 0x0Fu; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { code = c & 0x07u; extra = 3; }
        else { code = 0xFFFD; extra = 0; }
        if (i + extra >= text.size()) { code = 0xFFFD; extra = 0; }
        for (size_t k = 1; k <= extra; ++k) {
            const unsigned char cc = static_cast<unsigned char>(text[i + k]);
            if ((cc & 0xC0) != 0x80) { code = 0xFFFD; extra = 0; break; }
            code = (code << 6) | (cc & 0x3Fu);
        }
        i += extra + 1;
        if (code >= 0x10000) {
            code -= 0x10000;
            out.push_back(static_cast<uint16_t>(0xD800 + (code >> 10)));
            out.push_back(static_cast<uint16_t>(0xDC00 + (code & 0x3FF)));
        } else {
            out.push_back(static_cast<uint16_t>(code));
        }
    }
    return out;
}

int main() {
    void* workspace = vt_core_workspace_create();
    vt_options options{};
    vt_core_default_options(&options);
    options.StructSize = sizeof(vt_options);
    int c; std::string source; bool has_mode = false;
    // The user defined macros of the formula that follows.  Their names and
    // bodies have to outlive the call, so they are kept next to the array.
    std::vector<std::vector<uint16_t>> macro_names, macro_bodies;
    std::vector<vt_macro> macros;
    while ((c = std::getchar()) != EOF) {
        if (c == '\\n') {
            if (has_mode) {
                const std::vector<uint16_t> utf16 = DecodeUtf8(source);
                void* result = nullptr; vt_diagnostic diagnostic{};
                int32_t status = vt_core_compile(workspace,
                    vt_string16{utf16.data(), (int32_t)utf16.size()}, &options, 0, &result, &diagnostic);
                std::fwrite("\\x1e", 1, 1, stdout);
                if (status != 0) {
                    std::printf("!ERROR %d", status);
                } else {
                    vt_string8 output{};
                    vt_core_result_mathml(result, 1, &output);
                    std::fwrite(output.Data, 1, (size_t)output.Length, stdout);
                    vt_core_result_destroy(result);
                }
            }
            source.clear(); has_mode = false; continue;
        }
        if (!has_mode) {
            if (c == 'X') {
                // The macros of the previous formula are dropped.
                macros.clear(); macro_names.clear(); macro_bodies.clear();
                options.Macros = nullptr; options.MacroCount = 0;
                continue;
            }
            if (c == 'M') {
                // The rest of the line is "name\\tbody".
                std::string rest;
                while ((c = std::getchar()) != EOF && c != '\\n') rest.push_back((char)c);
                const size_t tab = rest.find('\\t');
                if (tab != std::string::npos) {
                    macro_names.push_back(DecodeUtf8(rest.substr(0, tab)));
                    macro_bodies.push_back(DecodeUtf8(rest.substr(tab + 1)));
                    macros.push_back({vt_string16{macro_names.back().data(), (int32_t)macro_names.back().size()},
                                      vt_string16{macro_bodies.back().data(), (int32_t)macro_bodies.back().size()}});
                    options.Macros = macros.data();
                    options.MacroCount = (int32_t)macros.size();
                }
                continue;
            }
            options.DisplayMode = (c == 'D') ? 1 : 0;
            has_mode = true; continue;
        }
        source.push_back(c == 1 ? '\\n' : (char)c);
    }
    vt_core_workspace_destroy(workspace);
    return 0;
}
`
  );
  execFileSync('cmake', ['-S', nativeDir, '-B', buildDir, '-DCMAKE_BUILD_TYPE=Release'], {
    stdio: 'ignore',
  });
  execFileSync('cmake', ['--build', buildDir, '-j8'], { stdio: 'ignore' });
  execFileSync(
    'g++',
    [
      '-std=c++20',
      '-O2',
      '-I',
      path.join(nativeDir, 'core'),
      source,
      '-o',
      probe,
      '-L',
      buildDir,
      '-lVelociTex.Core.Native',
      `-Wl,-rpath,${buildDir}`,
    ],
    { stdio: 'ignore' }
  );
}

function curatedCorpus() {
  const cases = [];
  const goldenPath = path.join(root, 'tests/native/katex-html-corpus.json');
  if (fs.existsSync(goldenPath)) {
    for (const entry of JSON.parse(fs.readFileSync(goldenPath, 'utf8'))) {
      // The corpus writes the flag under either name; `displayMode` wins when
      // both are present, so a `display: true` entry is not silently run in
      // text mode.
      cases.push({ tex: entry.tex, display: !!(entry.displayMode ?? entry.display) });
    }
  }
  return cases;
}

function realWorldCorpus() {
  const cases = [];
  const realPath = path.join(root, 'tests/native/katex-realworld-corpus.json');
  if (fs.existsSync(realPath)) {
    for (const tex of JSON.parse(fs.readFileSync(realPath, 'utf8'))) {
      cases.push({ tex, display: false });
    }
  }
  return cases;
}

function workloadCorpus() {
  const cases = [];
  const workloads = JSON.parse(fs.readFileSync(path.join(root, 'benchmarks/workloads.json'), 'utf8')).workloads;
  for (const group of Object.values(workloads)) {
    for (const item of group.cases) {
      if (item.latex) cases.push({ tex: item.latex, display: !!item.displayMode });
      for (const paragraph of item.paragraphs ?? []) {
        for (const part of paragraph) {
          if (part.latex) cases.push({ tex: part.latex, display: !!part.displayMode });
        }
      }
    }
  }
  return cases;
}

// The repository's own fixture corpus.  It covers every registered KaTeX
// symbol, function, macro and environment, so it is the broadest regression
// net of the three corpora.
function fixtureCorpus() {
  const cases = [];
  const fixturePath = path.join(root, 'tests/fixtures/core.json');
  if (fs.existsSync(fixturePath)) {
    const document = JSON.parse(fs.readFileSync(fixturePath, 'utf8'));
    for (const entry of document.cases ?? []) {
      cases.push({ tex: entry.latex, display: !!entry.displayMode, macros: entry.macros });
    }
  }
  return cases;
}

build();
const corpora = [];
if (corpusChoice === 'all' || corpusChoice === 'curated') corpora.push(['curated', curatedCorpus()]);
if (corpusChoice === 'all' || corpusChoice === 'realworld') corpora.push(['realworld', realWorldCorpus()]);
if (corpusChoice === 'all' || corpusChoice === 'workloads') corpora.push(['workloads', workloadCorpus()]);
if (corpusChoice === 'all' || corpusChoice === 'fixtures') corpora.push(['fixtures', fixtureCorpus()]);

let total = 0;
let identical = 0;
const failures = [];
for (const [name, cases] of corpora) {
  if (cases.length === 0) continue;
  // The probe reads one formula per line; a line that starts with 'M' defines
  // a macro and a line that starts with 'X' drops the macros of the previous
  // formula, so every formula sees only its own macros.
  const input =
    cases
      .map((c) => {
        const lines = ['X'];
        for (const [name, body] of Object.entries(c.macros ?? {})) {
          lines.push('M' + name + '\t' + body);
        }
        lines.push((c.display ? 'D' : 'T') + c.tex.replace(/\n/g, '\x01'));
        return lines.join('\n');
      })
      .join('\n') + '\n';
  const actual = execFileSync(probe, { input, maxBuffer: 1 << 28 })
    .toString()
    .split('\x1e')
    .slice(1);

  let groupIdentical = 0;
  for (let i = 0; i < cases.length; i++) {
    let expected;
    let expectedError = false;
    try {
      expected = katex.renderToString(cases[i].tex, {
        displayMode: cases[i].display,
        output: 'mathml',
        strict: 'ignore',
        ...(cases[i].macros ? { macros: cases[i].macros } : {}),
      });
    } catch (error) {
      expected = `ERROR: ${error.message}`;
      expectedError = true;
    }
    // KaTeX's MathML-only output is a plain `katex` span even in display
    // mode: `buildMathML` is returned before the display wrapper is applied.
    const prefix = '<span class="katex">';
    const suffix = '</span>';
    const produced = prefix + (actual[i] ?? '') + suffix;
    if (produced === expected) {
      groupIdentical++;
      identical++;
    } else if (expectedError && produced.startsWith(prefix + '!ERROR')) {
      groupIdentical++;
      identical++;
    } else {
      failures.push({ corpus: name, tex: cases[i].tex, produced, expected });
    }
    total++;
  }
  console.log(`mathml identical [${name}]: ${groupIdentical}/${cases.length}`);
}

for (const failure of failures.slice(0, verbose ? failures.length : limit)) {
  console.log(`\n--- tex: ${failure.tex}`);
  console.log(`  actual  : ${failure.produced}`);
  console.log(`  expected: ${failure.expected}`);
}
if (!verbose) {
  console.log('\nfailing:');
  for (const failure of failures) console.log(`  * [${failure.corpus}] ${failure.tex}`);
}
console.log(`\nmathml identical: ${identical}/${total}`);
process.exit(failures.length === 0 ? 0 : 1);
