# Performance benchmarks

These harnesses compare the same frozen workload across three presentation paths:

| Harness | Path | Timed region |
| --- | --- | --- |
| `VelociTex.Benchmarks` | Native AOT Core + portable SVG / MathML | LaTeX through validation, macros, parse, layout, shaping and requested export |
| `katex-browser/bench.mjs` | KaTeX 0.19.0 in Chrome | `renderToString`, DOM render, and 100-formula page |
| `wasm-browser/bench.mjs` | VelociTex WASM in Chrome | WASM compile, SVG/MathML export, DOM render, and 100-formula page |
| `samples/VelociTex.WinUI.Sample --perf` | Native WinUI `MathView` | `Result` assignment through Direct2D draw and Composition frames; includes a 100-formula page |

The shared corpus is [corpus.normal.json](corpus.normal.json): 60 formulas in six
categories (short, fractions/radicals, calculus, aligned, matrices, text). The
harnesses add a complex subset and a deterministic 100-formula page (60 corpus
entries plus 40 generated fractions/matrices/integrals). No formula-result cache
is enabled, and the same formula set is used for both implementations.

## Native Core

```powershell
dotnet publish benchmarks/VelociTex.Benchmarks/VelociTex.Benchmarks.csproj `
  -c Release -r win-x64 -p:Platform=x64 -p:PublishAot=true -o artifacts/performance/publish
./artifacts/performance/publish/VelociTex.Benchmarks.exe `
  --out artifacts/performance/core-final/run-1 --samples 6000 --core 0
```

`--samples` is the number of complete calls; six categories are shuffled by a
fixed seed. The report separates `compile`, `compile+mathml`, `compile+svg`
(Direct2D), `compile+portable-svg`, and `mathml-only` (semantics without layout).
It records P50/P95/P99/maximum, mean, and managed allocation. `--stages` profiles
macro, parse, node ownership, layout and MathML serialization; `--layout-profile`
adds internal layout phases.

## KaTeX 0.19.0

Prerequisites: Node.js 22+ and Chrome. The locked KaTeX distribution is the npm
0.19.0 tarball (SHA-256 `d8e49f2f…c96d1ee`); place it at
`artifacts/tooling/katex/package` or pass `--katex-dir`.

```powershell
node benchmarks/katex-browser/bench.mjs `
  --out artifacts/performance/katex --iterations 300 --e2e-iterations 20
```

The page is served with COOP/COEP so `performance.now()` is not clamped to
100 µs. Parse numbers come from `katex.renderToString` with the fixed corpus
options. DOM numbers force layout and then wait for two animation frames so the
frame containing the change has been painted. The frame interval is recorded.

## WASM

```powershell
./build-wasm.ps1 -Locked
node benchmarks/wasm-browser/bench.mjs `
  --out artifacts/performance/wasm --iterations 200 --page-rounds 5
```

The WASM harness serves `npm/velocitex/dist` and the locked KaTeX distribution
from one local HTTP server with COOP/COEP. It measures:

- `renderToString` to SVG and MathML, with a matching KaTeX `renderToString`;
- the same complex subset used by the native harness;
- single-formula `render()` into a live element, including forced layout and
  painted frames;
- a 100-formula page, measured both to completed DOM/layout and to painted frame;
- diagnostic splits (`compileSimple`, `compileOnly`, `svgSplit`, `interopPing`)
  used to separate C# compile, options JSON, SVG serialization and JS/WASM
  interop costs.

Formulas containing non-ASCII text are excluded from the WASM comparison because
the portable browser runtime deliberately has no host text shaper; both engines
are timed on the identical supported subset. Native and WinUI harnesses retain
the complete corpus.

## WinUI end-to-end

```powershell
dotnet publish samples/VelociTex.WinUI.Sample/VelociTex.WinUI.Sample.csproj `
  -c Release -p:Platform=x64 -o artifacts/winui-perf-aot
./artifacts/winui-perf-aot/VelociTex.WinUI.Sample.exe `
  --perf artifacts/performance/winui-final
```

The single-formula loop records the synchronous return after preparation and
Direct2D drawing into the Composition surface, then subsequent
`CompositionTarget.Rendering` callbacks and the observed compositor interval.
The page workload creates 100 `MathView` controls on one UI thread and measures
the complete assignment pass plus its first and second frames. Shared DirectWrite
faces, WARP device and `CompositionGraphicsDevice` are acquired once per page.
A real-window screenshot is attempted with Windows.Graphics.Capture and falls
back to `PrintWindow`; automated capture can be denied in non-interactive
sessions.

Frame-bound results depend on the display/compositor cadence and are reported
separately from CPU drawing time. Do not compare a 32 Hz remote-session frame
directly with a 60 Hz headless-browser frame; divide by the recorded frame
interval first.

## Measured environment

The current records were produced on a 2-core / 4-thread Intel Xeon Platinum VM
at 2.5 GHz, Windows Server 2022 build 20348, Chrome 128.0.6613.138, .NET SDK
10.0.402 and Native AOT 10.0.13. The machine is shared development hardware, not
the designated old-CPU acceptance machine.
