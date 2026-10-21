# HTML/CSS and native benchmarks

The current campaign is [results/2026-10-21-html-css](results/2026-10-21-html-css).
It measures the updated default HTML/CSS WebAssembly binding at source revision
`475c5d7`, alongside KaTeX 0.19.0 and freshly built native Core hosts.
[Tables, charts and rendering previews](../docs/benchmarks.md) are regenerated
from the validated raw batches. Preview6's MathML browser path remains in
[results/2026-10-20-preview6](results/2026-10-20-preview6) as historical data.

## What is measured

| Metric                    | Browser WASM and KaTeX                                                                            | Native C++ / .NET                                                                                                         |
| ------------------------- | ------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| HTML generation           | Public `renderToString` with default HTML + accessibility MathML; the same locked KaTeX CSS/fonts | No equivalent HTML API; excluded from this comparison                                                                     |
| MathML diagnostic         | Public `renderToString({ output: 'mathml' })`                                                     | C++ public C ABI and `.NET TryCompileToMathMl`                                                                            |
| Render work               | Fresh public `render`, DOM replacement and forced `getBoundingClientRect` after every formula     | Fresh compile, `MathView.Result` assignment with synchronous Direct2D submission, then `UpdateLayout` after every formula |
| Export/layout diagnostics | Separate from the browser comparison                                                              | Compile + mathematical layout; compile + standalone SVG export                                                            |

The current default browser generator is the standalone Rust-derived
KaTeX-compatible WASM engine, not the previous C++ MathML fast path. All 36
formula occurrences must produce byte-identical default markup to KaTeX before
timing. Route maps record `katex-html-wasm`; the harness fails if the new module
is unavailable or the output differs. MathML is retained as a supplemental
serialization metric, not used as a substitute for HTML generation.

Both browser engines use only `displayMode` and `throwOnError`, plus `output`
for the MathML diagnostic. Default HTML includes the accessibility MathML tree
on both engines; HTML-only output is a different workload. The bundled
`runtime/katex.min.css` and fonts are loaded before timing. A 24 px mathematical
em is used throughout, overriding KaTeX's default 1.21em enlargement.

The pure native C++ row calls the public C ABI directly in an `/O2` executable,
without CLR, graphics API or window. It includes parsing, MathML serialization,
copying to an owned UTF-8 string and destroying the result. The native Core does
not implement the browser HTML generator. Native UI figures remain separate
backend diagnostics and can include graphics queue backpressure.

DOM timing includes synchronous layout, excluding browser rasterization,
compositing, GPU completion and screen presentation. No benchmark waits for
`requestAnimationFrame`, vsync, or a display refresh. Native surface submission
does not fence GPU completion. Native .NET hosts are Release x64 JIT; the legacy
SVG/Core browser fallback is built Release WASM AOT, but is not exercised by
these default HTML cases.

Page scaffolding and body text are created outside timing. Each page operation
recompiles and renders every formula. Native uses horizontal text/formula rows;
browser paragraphs can wrap. Page figures describe formula work within a mixed
page, not full-document construction or body-text layout.

## Corpus and statistics

The unchanged [workloads.json](workloads.json) contains 6 common formulas,
3 complex formulas and 3 mixed pages with 10, 9 and 8 formulas: **36 formula
occurrences**. Cases cannot be skipped. Every rendered box must be nonzero.

1. Initialize modules, fonts and surfaces outside timing. Warm each case for at
   least 200 ms. Startup, download and first-render latency are excluded.
2. Double an operation batch until it takes at least 25 ms. One operation is one
   complete formula or complete page. Use that fixed batch size for measurement.
3. Collect 15 batches per metric and case using monotonic clocks. Public WASM
   Promise overhead stays inside timing; scheduling yields stay outside.
   Subsequent batches can take less than the calibration target.
4. Recompile and replace/draw each result on every operation. No document cache
   or precompiled result is used. Warm fonts/runtime caches and normal GC remain
   enabled; repeated warm inputs are not cold or adversarial input streams.
5. Repeat in three fresh processes per engine, rotating native/WASM/KaTeX/WinUI
   order between repetitions. Timing processes run sequentially without builds
   or other benchmarks running concurrently.
6. Take each case's median batch-average milliseconds within a process. Sum case
   medians and divide by the actual formula count (6, 3 or 27). The headline is
   the median of three process aggregates; whiskers show their min/max.
   Throughput is `1000 / msPerFormula`.

The pure C++ ABI campaign runs three additional fresh processes sequentially
following the four-host rotation, using the same native DLL (hash checked),
corpus and batch settings. Its separate `cpp-manifest.json` records compiler,
source/executable hashes and timing boundaries. Batch distributions are not
individual-call latency percentiles.

Schema 3 identifies default HTML/CSS browser reports and the campaign manifest;
unchanged native reports retain schema 2. The aggregator checks both versions,
output contracts, corpus hashes, exact case counts, routes and raw batches.
It rejects old browser MathML-path reports in a current campaign. The manifest
hashes the corpus, harnesses, native DLL, executables and complete browser dist.
Source revision plus `sourceHasChanges` and exact hashes identify the measured
working tree. Results describe one machine and corpus.

## Reproduce

Prerequisites: Windows x64, .NET SDK from `global.json`, `wasm-tools`, Visual
Studio C++ x64 tools, Node.js 22+, Chrome and Python. Rust builds the HTML WASM
from source when available; otherwise the build uses the committed prebuilt
module. Build logs identify which was used. `tools/reference/acquire.mjs`
acquires and verifies the locked KaTeX 0.19.0 baseline.

```powershell
npm ci
npm ci --prefix tools/reference
node tools/reference/acquire.mjs
python -m venv artifacts/benchmarks/venv
artifacts/benchmarks/venv/Scripts/python.exe -m pip install -r benchmarks/requirements.txt

# Builds Core, both native hosts and Release WASM AOT, then runs the campaign.
./benchmarks/run-benchmarks.ps1 -Output benchmarks/results/2026-10-21-html-css
# For an already freshly built distribution, add -SkipBuild.
# Choose a fresh output directory; existing manifests cannot be overwritten.
./benchmarks/build-native-benchmark.ps1
./benchmarks/run-cpp-benchmark.ps1 -Output benchmarks/results/2026-10-21-html-css
artifacts/benchmarks/venv/Scripts/python.exe benchmarks/plot_benchmarks.py --data benchmarks/results/2026-10-21-html-css
```

Standalone hosts accept `--samples 15 --batch-ms 25 --out <directory>`.
`browser/bench.mjs` also accepts `--engine velocitex|katex`, `--vt-dist`,
`--katex-dir` and `--chrome`.

## Rendering preview and parity

```powershell
node tests/wasm/katex-parity.mjs
$env:VELOCITEX_BROWSER = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
node tests/wasm/katex-browser-parity.mjs
$env:VELOCITEX_NATIVE_DIR = "$PWD/artifacts/core-native"
./benchmarks/VelociTex.Benchmarks/bin/x64/Release/net10.0-windows10.0.26100.0/win-x64/VelociTex.Benchmarks.exe --render-comparison artifacts/comparison-html-css
node benchmarks/browser/capture-comparison.mjs --out artifacts/comparison-html-css
Copy-Item artifacts/comparison-html-css/browser-validation.json benchmarks/results/2026-10-21-html-css/rendering-validation.json
artifacts/benchmarks/venv/Scripts/python.exe benchmarks/prepare_comparison.py --data artifacts/comparison-html-css
artifacts/benchmarks/venv/Scripts/python.exe benchmarks/update_html_docs.py
```

Actual native/WASM/KaTeX outputs use a 32 px math em, shared foreground and 2×
pixel scale. The browser capture records markup agreement and requires matching
computed styles and dimensions in the visible HTML trees before saving its
validation report and screenshots. Three previews have byte-identical markup;
the combined color example differs in CSS declaration order and a redundant
inherited color declaration. These images are not asserted pixel-identical.
`prepare_comparison.py` crops only
surrounding blank space and assigns all cells in each row the same canvas size;
it never stretches glyphs independently. Repository and package READMEs include
the new tables and charts. `update_html_docs.py` refreshes current claims without
rewriting preview6 release notes. The old `update_docs.py` is for historical
preview6 reproduction only.
