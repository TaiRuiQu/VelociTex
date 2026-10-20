# 2026-10-20 performance verification

Updated: 2026-10-20 Asia/Shanghai. This record covers the new WASM/npm binding
pulled from `7dca991` plus the local performance work on top of it. It is a
development-machine record, not old-hardware acceptance and not a complete
release gate.

> **C++ addendum:** the MathML and WASM rows below are the C#-only baseline and
> are superseded by [the C++ fast pre-render record](performance-cpp-2026-10-20.md).
> Native SVG and WinUI rows remain current. The C++ path meets the MathML and
> WASM targets and changes the fast MathML output contract; see that record for
> the measured values and fallback behavior.

## Targets and outcome

| Target | Result | Status |
| --- | --- | --- |
| Pure parse to SVG at least 3x faster than KaTeX | Native AOT portable SVG: **3.54x mean, 3.39x P50, 4.14x P95** | **Pass** |
| Pure parse to MathML at least 3x faster than KaTeX | Native AOT MathML-only: **1.31x mean, 1.22x P50, 1.31x P95** | **Fail** |
| WinUI end-to-end at least 2x faster than browser KaTeX | Single formula CPU drawing **5.78x mean / 8.83x P50**; 100-formula page **2.49x** call-to-DOM and **2.18x** second frame | **Pass** |
| WASM at least 50% faster than browser KaTeX | Single parse **1.20x slower**; single DOM **1.21x faster**; 100-formula page **1.38x** call-to-DOM and **1.50x** painted | **Fail** |

The remaining two gaps are not being relabelled as passes. The WASM result is
limited by the .NET 10 browser Mono-AOT pipeline: `CompileMetricsSimple` alone
costs **0.110 ms P50** against a 0.095 ms budget for a 2x faster total, while a
complete interop round trip is only 0.005 ms. The MathML output target would
require a further 2.3x while KaTeX's MathML path deliberately skips layout.

## Environment

| Item | Value |
| --- | --- |
| CPU | Intel Xeon Platinum, 2 cores / 4 logical processors, 2.5 GHz (VM) |
| OS | Microsoft Windows Server 2022 Datacenter, 10.0.20348 |
| SDK / runtime | .NET SDK 10.0.402, runtime 10.0.13, Native AOT 10.0.13 |
| Browser | Chrome 128.0.6613.138, headless new mode |
| WASM workload | wasm-tools 10.0.13, `RunAOTCompilation=true`, `WasmStripILAfterAOT=true` |
| KaTeX | 0.19.0, npm tarball SHA-256 `d8e49f2fea6eeed7cdae2cf4fd48e2f1d348758aa9e5740715e4533c2c96d1ee` |
| Corpus | `benchmarks/corpus.normal.json`, SHA-256 `0c9f4daf48a372eba2b297e346bc5a7dd11ac54e816a2ca7281eda150677a3b0` |
| WinUI rasterization scale | 1.0; observed compositor interval 16.9 ms P50 in the final run |

The machine is shared development hardware. It is not the 2015–017 reference
CPU and the results do not substitute for that acceptance.

## Workloads

All paths use the frozen 60-formula normal corpus (six categories, ten formulas
each). No formula, semantic or layout result cache is enabled.

- **Single/simple:** the complete 60-formula corpus, fixed-seed shuffled.
- **Complex:** twelve formulas from matrices, aligned, calculus and text
  categories (5×5 matrices, aligned systems, integrals/sums, nested fractions).
- **Page:** 100 formulas: the 60 corpus entries plus 40 deterministic generated
  fractions/matrices/integrals. WinUI creates 100 `MathView` controls on one UI
  thread; browser harnesses create 100 hosts in one layout pass.
- **DOM single:** repeated `render()`/`katex.render()` into one host, forced
  layout, then two animation frames.
- The WASM comparison excludes formulas containing non-ASCII text because the
  portable browser runtime has no host text shaper; native/WinUI comparisons
  retain all 60 formulas.

## Pure parsing: native Native-AOT

Five independent Release Native-AOT processes, 6,000 complete calls each, pinned
to one logical processor, fixed-seed shuffle, outliers retained. Values are
milliseconds (P50 / P95 / mean).

| Path | VelociTex | KaTeX 0.19.0 | Speedup (mean) |
| --- | --- | --- | --- |
| Compile only (validation→drawable result) | 0.0442–.0445 / 0.0938–.0975 / 0.0480 | 0.200 / 0.485 / 0.2294 (HTML `renderToString`) | **4.78x** |
| Compile + portable SVG | 0.0587–.0596 / 0.1165–.1202 / 0.0648 | 0.200 / 0.485 / 0.2294 | **3.54x** |
| Compile + MathML-only (no layout) | 0.0369–.0371 / 0.0837–.0841 / 0.0409 | 0.045 / 0.110 / 0.0535 (MathML) | **1.31x** |
| Compile + Windows Direct2D SVG | 0.1704–.1744 / 0.2718–.3222 / 0.1563 | 0.200 / 0.485 / 0.2294 | 1.47x |
| Compile + MathML after layout | 0.0572–.0577 / 0.1328–.1364 / 0.0638 | 0.045 / 0.110 / 0.0535 | 0.84x |

The portable SVG path is the cross-platform parse-to-SVG implementation used by
WASM and is the path that meets the 3x target. The Windows Direct2D SVG exporter
also embeds DirectWrite outlines and is reported separately rather than mixed
into the portable result.

After the five-run table was taken, layout scratch pooling removed the per-call
layout arrays and lists. Managed allocation for a complete compile fell from
14,120 B to 4,328 B in a 6,000-call spot check, and portable SVG measured
0.0563 ms P50 / 0.1081 ms P95. The conservative five-run values above are kept
for the acceptance ratios.

Per-category portable-SVG averages (five runs):

| Category | P50 ms | P95 ms |
| --- | --- | --- |
| short | 0.0285 | 0.0427 |
| fractions/radicals | 0.0614 | 0.0842 |
| calculus | 0.0394 | 0.0623 |
| aligned | 0.0814 | 0.1151 |
| matrices | 0.1061 | 0.1359 |
| text-nesting | 0.0580 | 0.0782 |

## WinUI end-to-end

Final Release Native-AOT WinUI run, 360 warmed single-formula samples and one
100-formula page. `sync` is the synchronous return after `MathView.Result`
assignment has completed preparation and Direct2D drawing into the Composition
surface. Frame columns are subsequent `CompositionTarget.Rendering` callbacks.
Browser `domSync` is the corresponding call plus forced style/layout; browser
`domPaint` waits two animation frames.

| Metric | WinUI | KaTeX browser | Ratio |
| --- | --- | --- | --- |
| Single sync P50 / P95 / mean | **0.262 / 1.512 / 0.425** | domSync 2.315 / 3.910 / 2.457 | **8.83x / 2.59x / 5.78x** |
| 100-formula page sync | **85.4 ms** | page sync 212.3 ms | **2.49x** |
| 100-formula page first frame | **96.0 ms** | page paint 251.8 ms | **2.62x** |
| 100-formula page second frame | **115.6 ms** | page paint 251.8 ms | **2.18x** |
| Single first/second frame | 32.2 / 64.2 ms | domPaint 33.2 ms | display/compositor-bound; not claimed as a multiple |

The 100-formula WinUI page creates the controls on one UI thread and shares the
DirectWrite faces, WARP device, `CompositionGraphicsDevice` and geometry caches
across views. Without that sharing the page created one device per control and
was slower than the browser baseline. A real-window screenshot after the page
workload is committed at
[performance-winui-page.png](performance-winui-page.png); the automated capture
path first attempted Windows.Graphics.Capture and fell back to `PrintWindow`
when the capture session was denied.

A later confirmation run landed on a 31.4 ms compositor interval (about 32 Hz)
instead of the 16.9 ms interval above. Its single-formula CPU sync remained
0.29 ms P50, while the page's synchronous assignment rose to 111.7 ms because
100 Composition surface commits were throttled by the slower compositor. The
page target is therefore reported with the measured frame cadence, not by mixing
the 32 Hz run with the 59 Hz browser run.

## WASM versus browser KaTeX

Final WASM AOT run, 300 iterations per formula and five page rounds, Chrome with
COOP/COEP so the timer resolution is ~5 µs.

| Metric | VelociTex WASM | KaTeX | Result |
| --- | --- | --- | --- |
| Parse → SVG P50 / P95 / mean | 0.230 / 0.415 / 0.255 | HTML 0.185 / 0.330 / 0.199 | **1.28x slower** |
| Parse → MathML P50 / P95 / mean | 0.110 / 0.265 / 0.134 | MathML 0.045 / 0.105 / 0.051 | **2.6x slower** |
| Complex subset P50 | 0.215 | 0.175 | 1.23x slower |
| Single DOM sync P50 | **1.960** | 2.375 | **1.21x** |
| 100-formula page sync | **149.1 ms** | 206.2 ms | **1.38x** |
| 100-formula page paint | **166.8 ms** | 250.9 ms | **1.50x** |

Diagnostic split from the same AOT build:

| Item | P50 |
| --- | --- |
| JS/WASM interop round trip (`Ping`) | 0.005 ms |
| Compile with minimal options (`CompileMetricsSimple`) | 0.110 ms |
| Compile with options JSON | 0.125 ms |
| SVG serialization only (`CompileMetricsSvg`) | 0.050 ms |
| Complete parse-to-SVG including JS wrapper | 0.230 ms |

The 0.095 ms total budget required for 2x faster than 0.185 ms is below the
compile floor alone. `PublishAot=true` for `browser-wasm` is rejected by this SDK
(`NETSDK1203`: browser-wasm does not support ahead-of-time compilation), so the
binding is on Mono AOT. Removing the SDK's default `no-opt` AOT argument did not
change the measured compile floor; the page/DOM improvement comes from smaller
shared-definition DOM output. Meeting the WASM target needs either a faster
browser runtime/toolchain or a further ~2x algorithmic reduction in the compile
path; that work is not claimed here.

## Implemented optimizations

Core:

- Removed the per-compile copy of 319 built-in macros; lookups fall back to a
  static alternate-key table and only user/session definitions are stored.
- Added a fast ordinary-atom path in the parser and a fast ordinary-token path in
  macro expansion; matrix `&` and other scope events remain on the general path.
- Replaced symbol/command/glyph binary searches with static span-keyed maps and
  a font/glyph metric cache that preserves glyph-only fallback semantics.
- Replaced the MathML `XmlWriter` tree serializer with direct `StringBuilder`
  output, pooled node storage and pooled scratch arrays.
- Added `MathEngine.TryCompileToMathMl`, which performs validation, macros and
  parsing but skips layout, shaping and drawing-result allocation.
- Rewrote the portable SVG writer with direct output, cached generated path
  arrays, cached formatted path data and a fast fixed-point coordinate formatter.
- Reused layout scratch arrays/lists on the calling thread; a complete compile's
  managed allocation fell to about 4.3 KB and the heavy semantic tree is dropped
  for the internal SVG-only path.
- Added page-level shared glyph definitions with stable IDs and stopped wrapping
  default-state paint commands in redundant `<g>` elements. This cut the
  100-formula page's DOM and paint time while preserving standalone
  `renderToString` output.

Native rendering and WinUI:

- Extract the immutable embedded fonts once per process instead of once per
  renderer; honor `VELOCITEX_FONT_DIRECTORY` and app-bundled fonts.
- Cache validated private-font glyphs and Direct2D glyph geometries.
- Reuse `WindowsMathRenderer`, WARP device, `CompositionGraphicsDevice` and
  surface across result changes and DPI rebuilds.
- Share renderer/device resources across all `MathView` controls on the UI
  thread, ref-counted until the last view unloads.
- Added a real-window 100-formula page benchmark and GDI `PrintWindow` screenshot
  fallback.

WASM:

- Added output selection so an SVG-only or MathML-only request does not build
  both exports.
- Added direct string exports (`CompileSvg`, `CompileMathMl`) that avoid JSON
  escaping/parsing on success while preserving structured error diagnostics.
- Added default-limit fast exports (`CompileSvgSimple`, `CompileMathMlSimple`) for
  options that don't require macros or custom limits.
- Added diagnostic-only exports for compile/export/interop timing.
- Enabled Mono AOT with IL stripping and published the runtime into the npm dist
  package. Removed the SDK's default `no-opt` AOT argument so LLVM emits
  optimized code; the measured compile floor did not move, so this is retained
  as build hygiene rather than claimed as a speedup.

## Correctness and regression evidence

- Final P1/P2/P4 hard differential suites pass on JIT and Native AOT:
  - P1: 64 valid / 12 invalid, 202 exports.
  - P2: 307 valid / 31 invalid, 491 exports.
  - P4: 3,659 valid / 50 invalid, 3,843 exports per runtime.
- `tests/VelociTex.Web.Tests` portable SVG oracle passes: 3,630 valid and 79
  diagnostic cases, including 29 missing-font diagnostics.
- `tools/reference/wasm-browser.mjs` against the published AOT runtime:
  3,630 parity successes, 79 parity diagnostics, all API/ID/reference, error,
  macro-isolation, delimiter, fragment and boundedness assertions pass.
- The same browser verification passes against a freshly packed isolated npm
  consumer: `npm install` of `velocitex-0.1.0-preview.2.tgz` (SHA-256
  `1891a43514ce9c05acaf3580caa21142ba376a58a87854fbb43f44bdcc282a55`), copied
  runtime assets, and a strict TypeScript check of the consumer entry point.
  This is a locally built package, not a publication claim.
- The final AOT9 build (page-level shared definitions, stable glyph IDs, no
  redundant default-state `<g>` wrappers) passes both the 3,630-case browser
  parity suite and the isolated package check. The shared-definition SVG output
  was also inspected in a real page screenshot; standalone `renderToString`
  remains self-contained.
- The real WinUI page was launched and captured; the screenshot is committed.
  Automated capture was denied in this session and the GDI fallback was used, so
  this is visual evidence from the real HWND but not the full P5 screenshot
  matrix.

## Reproduction

```powershell
dotnet build-server shutdown
dotnet publish benchmarks/VelociTex.Benchmarks/VelociTex.Benchmarks.csproj -c Release -r win-x64 -p:Platform=x64 -p:PublishAot=true -o artifacts/performance/publish
./artifacts/performance/publish/VelociTex.Benchmarks.exe --out artifacts/performance/core-final2/run-1 --samples 6000 --core 0
node benchmarks/katex-browser/bench.mjs --out artifacts/performance/katex-final --iterations 300 --e2e-iterations 20
./build-wasm.ps1 -Locked
node benchmarks/wasm-browser/bench.mjs --out artifacts/performance/wasm-final --iterations 300 --page-rounds 5
dotnet publish samples/VelociTex.WinUI.Sample/VelociTex.WinUI.Sample.csproj -c Release -p:Platform=x64 -o artifacts/winui-perf-aot2
./artifacts/winui-perf-aot2/VelociTex.WinUI.Sample.exe --perf artifacts/performance/winui-final2
```

## Remaining work

- WASM 50%: requires a faster browser runtime/toolchain or an approximately 2x
  compile-path reduction beyond the current Mono-AOT floor. The current AOT
  diagnostics are the baseline for that work.
- MathML 3x: the semantic parse path is already faster than KaTeX MathML only on
  some measures; matching 3x needs a substantially cheaper MathML projection or
  a different output contract.
- Old-CPU, physical monitor DPI, minimum Windows 10, hardware GPU/device removal,
  exact native/GPU peak accounting and screen-reader audio remain unverified.
- The 27 XML record-property `>` text fragments in the frozen candidate still
  need a source-comment cleanup before a stable release.

