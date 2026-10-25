// Collects the profile that the WebAssembly build of the core uses.
//
// The compiler lays the code of a program out with the branches that the
// profile says are the hot ones next to each other.  A renderer has a few
// very hot paths and a lot of cold ones, and a compiler that does not know
// which is which spreads them over the whole text segment.  The profile
// tells it.
//
// The profile is collected from a build of the core that is instrumented for
// the profile and that renders the acceptance corpus of the repository (1605
// real world formulas).  The corpus of the benchmark is not used, so the
// layout is not tuned for the numbers of the benchmark.
//
// The build of the profile is a *native* build: the WebAssembly build of the
// Emscripten SDK has no profile runtime (the `libclang_rt.profile` of the
// wasm target is not part of it), and a profile of a native build is still
// the profile of the same source: the hash of a function is the hash of its
// name and of its structure, and both are the same for the two targets.  The
// build of the WebAssembly module reads the profile with `-fprofile-use` and
// the functions of the core match the ones of the profile.
//
// Usage: node tools/reference/generate-pgo-profile.mjs [--out <file>]
//
// The compiler and the profile merger are taken from the environment
// (CLANG and LLVM_PROFDATA) or from the PATH.
import { execFileSync, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(fileURLToPath(new URL('../../', import.meta.url)));
const core = path.join(root, 'native/core');
const outIndex = process.argv.indexOf('--out');
const out = path.resolve(outIndex >= 0 ? process.argv[outIndex + 1] : path.join(root, 'native/wasm/katex.pgo'));

// The sources of the core, in the order of the build scripts.
const sources = [
  'types.cpp',
  'lexer.cpp',
  'macros.cpp',
  'parser.cpp',
  'cache.cpp',
  'layout.cpp',
  'geometry.cpp',
  'mathml.cpp',
  'svg.cpp',
  'core.cpp',
  'generated/data.cpp',
  'generated/svg_data.cpp',
  'html/domtree.cpp',
  'html/options.cpp',
  'html/build_common.cpp',
  'html/build_html.cpp',
  'html/svg.cpp',
  'html/delimiter.cpp',
  'html/accent.cpp',
  'html/array.cpp',
  'html/enclose.cpp',
  'generated/html_paths.cpp',
  'generated/html_sqrt_paths.cpp',
  'pool_alloc.cpp',
];

// Finds a tool: the environment names it, or the PATH holds it under one of
// the names that are tried.
function findTool(envName, names) {
  const named = process.env[envName];
  if (named) {
    if (!fs.existsSync(named)) throw new Error(`${envName} does not name a file: ${named}`);
    return named;
  }
  for (const name of names) {
    // The tool is only asked whether it runs: `llvm-profdata` answers a
    // `--version` that it does not know with a non zero exit code.
    const probe = spawnSync(name, ['--version'], { encoding: 'utf8' });
    if (!probe.error) return name;
  }
  throw new Error(`No tool found: set ${envName} to one of ${names.join(', ')}.`);
}

const clang = findTool('CLANG', [
  'clang++',
  'clang++-20',
  'clang++-19',
  'clang++-18',
  'clang++-17',
  'clang++-16',
  'clang++-15',
  'clang++-14',
  'clang++-13',
]);
const profdata = findTool('LLVM_PROFDATA', [
  'llvm-profdata',
  'llvm-profdata-20',
  'llvm-profdata-19',
  'llvm-profdata-18',
  'llvm-profdata-17',
  'llvm-profdata-16',
  'llvm-profdata-15',
  'llvm-profdata-14',
  'llvm-profdata-13',
]);

// Some installations of clang do not find the headers and the library of the
// C++ standard library on their own.  The paths of the library are taken from
// the C++ compiler of the system when that is the case.
function libraryArguments() {
  const probe = spawnSync(clang, ['-x', 'c++', '-E', '-'], { input: '#include <algorithm>\n', encoding: 'utf8' });
  if (!probe.error && probe.status === 0) return [];
  const gxx = findTool('CXX', ['g++', 'g++-14', 'g++-13', 'g++-12', 'g++-11']);
  const listing = spawnSync(gxx, ['-E', '-x', 'c++', '-v', '-'], { input: '\n', encoding: 'utf8' });
  const paths = [];
  let inList = false;
  for (const line of (listing.stderr ?? '').split('\n')) {
    if (line.startsWith('#include <...> search starts here:')) {
      inList = true;
      continue;
    }
    if (inList && line.startsWith('End of search list.')) break;
    if (inList && line.trim()) paths.push(line.trim());
  }
  if (paths.length === 0) throw new Error('The headers of the C++ library were not found.');
  const arguments_ = paths.flatMap((path) => ['-isystem', path]);
  // The linker needs the directory of the library itself: the system carries
  // the shared object of the C++ library under a versioned name.
  const library = spawnSync(gxx, ['-print-file-name=libstdc++.so'], { encoding: 'utf8' });
  const libraryPath = (library.stdout ?? '').trim();
  if (libraryPath && libraryPath !== 'libstdc++.so' && fs.existsSync(libraryPath)) {
    arguments_.push('-L', path.dirname(libraryPath));
  }
  return arguments_;
}
const libraryPaths = libraryArguments();

// The corpus: the acceptance corpus of the repository, as the records of the
// training program (the length of the formula in bytes and the formula as
// UTF-16LE).
const corpus = JSON.parse(fs.readFileSync(path.join(root, 'tests/native/katex-realworld-corpus.json'), 'utf8'));
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'velocitex-pgo-'));
const corpusPath = path.join(work, 'corpus.bin');
const corpusBuffer = Buffer.alloc(corpus.reduce((sum, formula) => sum + 4 + Buffer.byteLength(formula, 'utf16le'), 0));
let cursor = 0;
for (const formula of corpus) {
  const bytes = Buffer.from(formula, 'utf16le');
  corpusBuffer.writeUInt32LE(bytes.length, cursor);
  cursor += 4;
  bytes.copy(corpusBuffer, cursor);
  cursor += bytes.length;
}
fs.writeFileSync(corpusPath, corpusBuffer);

// The training build: the core and the training program, instrumented for
// the profile.
const driver = path.join(work, 'training');
const compile = spawnSync(
  clang,
  [
    '-O3',
    '-flto',
    '-std=c++20',
    '-fprofile-instr-generate',
    `-fprofile-dir=${work}`,
    '-I',
    core,
    ...libraryPaths,
    path.join(root, 'native/pgo/training.cpp'),
    ...sources.map((source) => path.join(core, source)),
    '-o',
    driver,
  ],
  { encoding: 'utf8' }
);
if (compile.status !== 0) {
  process.stderr.write(compile.stdout ?? '');
  process.stderr.write(compile.stderr ?? '');
  throw new Error('The instrumented build of the training program failed.');
}

// The training itself: the profile is written when the program ends.
const raw = path.join(work, 'train.profraw');
const train = spawnSync(driver, [corpusPath, '2'], {
  encoding: 'utf8',
  env: { ...process.env, LLVM_PROFILE_FILE: raw },
});
if (train.status !== 0) {
  process.stderr.write(train.stdout ?? '');
  process.stderr.write(train.stderr ?? '');
  throw new Error('The training program failed.');
}
if (!fs.existsSync(raw)) throw new Error('The training program wrote no profile.');

// The merge: the compiler reads the merged profile.
const merged = path.join(work, 'core.profdata');
const merge = spawnSync(profdata, ['merge', '--sparse', '-o', merged, raw], { encoding: 'utf8' });
if (merge.status !== 0) {
  process.stderr.write(merge.stdout ?? '');
  process.stderr.write(merge.stderr ?? '');
  throw new Error('The merge of the profile failed.');
}
fs.copyFileSync(merged, out);
fs.rmSync(work, { recursive: true, force: true });
console.log(
  `Profile of the core: ${corpus.length} formulas, ${fs.statSync(out).size} bytes -> ${path.relative(root, out)}`
);
