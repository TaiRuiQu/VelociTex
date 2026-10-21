# Refresh-independent benchmarks

> **Historical note:** these preview6 numbers describe the old MathML fast path.
> The current default browser path emits byte-identical KaTeX 0.19.0 HTML/CSS and
> is verified by `tests/wasm/katex-parity.mjs`; use that parity gate for current
> HTML correctness claims. Native C++/Direct2D measurements remain relevant.

The preview6 benchmark measures the work the library performs. No headline
metric waits for `requestAnimationFrame`, `CompositionTarget.Rendering`, vsync,
or a monitor refresh. Native WinUI surface diagnostics can still include graphics
queue backpressure; they are not used as evidence of pure C++ engine throughput.
The previous frame-paced results remain in
[results/2026-10-20](results/2026-10-20) as historical data; they must not be mixed
with schema-v2 results.

## What is measured

| Metric             | Native C++ / .NET                                                                                                                                    | Browser C++ WASM + fallback                                                                     | KaTeX 0.19.0                                                                           |
| ------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| MathML generation  | `MathEngine.TryCompileToMathMl`: parse, macro expansion, complete MathML serialization                                                               | Public async `renderToString({ output: 'mathml' })`, default fast route with full-Core fallback | Public `renderToString({ output: 'mathml' })`                                          |
| Render work        | Fresh compile, `MathView.Result` assignment (including synchronous Direct2D drawing/surface submission), then `UpdateLayout` after **every** formula | Public async `render`, DOM replacement, then `getBoundingClientRect` after **every** formula    | Public `render`, DOM replacement, then `getBoundingClientRect` after **every** formula |
| Native diagnostics | Compile + mathematical layout; compile + standalone SVG export                                                                                       | —                                                                                               | —                                                                                      |

The primary native engine row/chart calls the **C++ public C ABI directly**, in a
standalone `/O2` executable with no CLR, graphics API or window. It includes parse,
MathML serialization, copying into an owned UTF-8 `std::string`, and destroying
the result. The .NET binding's MathML/compile/SVG figures remain in the raw data
and per-case appendix. This separates engine throughput from binding/UI overhead.

MathML is the common output contract. DOM rendering uses each library's default
presentation: browser MathML on the C++ fast path, exact SVG on fallback, and
KaTeX HTML/CSS/SVG. Native uses DirectWrite/Direct2D.
All paths use a 24 px mathematical em; the fixture overrides KaTeX's default
1.21em enlargement so it does not measure larger glyphs than the other engines.
Render-work figures compare these real backends' work, **not equivalent pixels or screen-presentation latency**.
Browser raster/paint and GPU completion are outside these measurements. Native
surface submission does not fence GPU completion. Native .NET hosts are Release
x64 JIT; the full fallback browser runtime is Release WASM AOT.

Page scaffolding (title, body text and formula hosts) is created before timing;
all page formulas are compiled and rendered inside each operation. Native uses
horizontal text/formula rows; browser paragraphs can wrap. These are formula
render-work figures within a mixed page, not full-document construction or a
body-text layout comparison.

The old harness passed explicit `strict`, `trust` and `maxExpand` defaults that
disabled the documented C++ fast-option path. Schema v2 uses only `displayMode`
and `throwOnError` on **both** libraries, plus `output` for MathML. Advanced options
are a different workload; these results do not claim their performance.

## Corpus and statistics

The unchanged [workloads.json](workloads.json) contains 6 common formulas, 3
complex formulas, and 3 mixed pages with 10, 9 and 8 formulas: **36 formula
occurrences**. No unsupported or slow formula may be skipped. Every case must
produce nonempty MathML and a nonzero rendered box. The browser report records
the C++ fast/fallback route for every occurrence. All fallback time is included.

1. Initialize modules, fonts and surfaces outside timing. Warm **each case** for
   at least 200 ms. Cold download, startup and first-render latency are not measured.
2. Calibrate a batch by doubling its operation count until a complete batch takes
   at least 25 ms. One operation is one complete formula or one complete page.
3. Collect 15 batches per metric and case. Use monotonic `Stopwatch` or isolated
   browser `performance.now()`. Browser Promise overhead remains inside timing;
   scheduling yields remain outside. No frame waits occur. Calibration selects
   a fixed batch size; subsequent batches can finish in less than 25 ms.
4. Each operation recompiles the source and replaces/draws the result. No
   `MathDocumentCache` or precompiled result is used. Warm font, geometry and
   runtime caches remain enabled; normal GC/finalization is included. Fixed-case
   batches measure repeated warm inputs, not cold or adversarial input streams.
5. Repeat in **3 fresh processes** per engine. Rotate engine order between
   repetitions and run timing processes sequentially. Do not build or run other
   benchmarks concurrently.
6. Within each process, take each case's median of batch-average milliseconds.
   Sum those case medians and divide by the **actual total formula count** (6,
   3 or 27). Headline values are the median of the 3 process aggregates. Chart
   whiskers show their min–max; throughput is `1000 / msPerFormula`.

The pure C++ ABI campaign runs three additional fresh processes sequentially
after the four-host rotated campaign, using the same native DLL (hash checked),
corpus and batch settings. Its separate `cpp-manifest.json` records compiler,
source/executable hashes and start/end time. It is not described as part of the
four-host order rotation.

Batch-average distributions are **not individual-call latency percentiles**.
The reports deliberately do not label them P95/P99. Every raw
batch's count/time, route maps, configurations and hardware accompany the results.
The manifest hashes the harness, corpus, native DLL and browser distribution.
Source revision plus `sourceHasChanges` honestly identifies a measurement made
while preparing a release; exact measured file hashes are retained.

## Reproduce

Prerequisites: Windows x64, .NET SDK pinned by `global.json`, the `wasm-tools`
workload, Visual Studio C++ x64 tools, Node.js 22+, and Chrome. The frozen KaTeX
0.19.0 distribution is acquired and verified by `tools/reference/acquire.mjs`
under `artifacts/upstream/npm/package`; browser scripts also accept `--katex-dir`
and `--chrome` overrides. Python dependencies are in `requirements.txt`.

```powershell
# Build native Core, both native hosts, and the Release WASM AOT package;
# then run three sequential process repetitions with rotated engine order.
./benchmarks/run-benchmarks.ps1 -Output artifacts/benchmarks/preview6

# Or reuse the exact freshly built distribution:
./benchmarks/run-benchmarks.ps1 -SkipBuild -Output artifacts/benchmarks/preview6

# Supplemental direct C++ ABI measurements (no CLR/window/GPU):
./benchmarks/build-native-benchmark.ps1
./benchmarks/run-cpp-benchmark.ps1 -Output artifacts/benchmarks/preview6

python -m pip install -r benchmarks/requirements.txt
python benchmarks/plot_benchmarks.py --data artifacts/benchmarks/preview6
```

Standalone hosts accept `--samples 15 --batch-ms 25 --out <directory>`.
`browser/bench.mjs` additionally takes `--engine velocitex|katex`.
The legacy hosts are available through `--legacy` for historical reproduction;
their frame-paced reports are not accepted by the new aggregator.

The official preview6 data are in
[results/2026-10-20-preview6](results/2026-10-20-preview6). See
[the result tables and charts](../docs/benchmarks.md) for measured values. This
is one machine and one corpus; results are not a promise for every device/input.

## Rendering comparison tables

Capture actual outputs with the same 32 px math em, foreground and 2× pixel scale:

```powershell
$env:VELOCITEX_NATIVE_DIR = "$PWD/artifacts/core-native"
./benchmarks/VelociTex.Benchmarks/bin/x64/Release/net10.0-windows10.0.26100.0/win-x64/VelociTex.Benchmarks.exe --render-comparison artifacts/comparison-preview6
node benchmarks/browser/capture-comparison.mjs --out artifacts/comparison-preview6
python benchmarks/prepare_comparison.py --data artifacts/comparison-preview6
python benchmarks/update_docs.py
```

`prepare_comparison.py` removes only surrounding blank space and gives all three
cells in each row the same canvas dimensions. It never independently rescales
formula glyphs. The READMEs use actual Markdown tables with accessible image
labels. npm bundles every cell and chart; NuGet's README uses immutable tagged
GitHub image URLs. `update_docs.py` regenerates every performance claim from the
validated summary, so package/repository numbers stay consistent.
