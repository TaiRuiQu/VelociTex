# C++ fast pre-render verification — 2026-10-20

This record supersedes the MathML and WASM rows of
[performance-2026-10-20.md](performance-2026-10-20.md). Native SVG and WinUI
numbers there remain the current values because those paths were not rewritten.

## Outcome

| Target | Measured | Status |
| --- | --- | --- |
| Pure parse to SVG at least 3x KaTeX | Native C# portable SVG: **3.54x mean** (unchanged) | Pass |
| Pure parse to MathML at least 3x KaTeX | Native C++ via P/Invoke, Native AOT: **14.2x–17.3x mean** vs KaTeX MathML | Pass |
| WinUI end-to-end at least 2x browser KaTeX | Single **5.78x mean**; 100-formula page **2.49x sync / 2.18x second frame** (unchanged) | Pass |
| WASM at least 50% faster than KaTeX | MathML parse **3.9x mean**; single DOM **3.47x**; 100-formula page **4.76x sync / 4.63x painted** | Pass |
| Single, complex and multi-formula workloads | 60 corpus, 12 complex, 100-formula page, all with the C++ path | Pass |

The WASM binding no longer depends on the C# Mono AOT pipeline for supported
formulas. A 24 KB C++ WebAssembly module performs the complete pre-render
pipeline: input scanning, command/macro dispatch, bounded parsing and
presentation-MathML serialization. The browser's native MathML engine performs
layout and painting. Unsupported syntax falls back to the full C# engine; the
fallback remains covered by the 3,630-case parity suite.

## Architecture

- `native/VelociTex.NativeMath.cpp` —allocation-light recursive-descent parser
  and MathML writer. One `thread_local` output buffer; no global mutable state;
  no AST; output length returned so callers can size a buffer.
- `src/VelociTex.NativeMath` —P/Invoke adapter with a DLL resolver, native
  AOT-compatible declarations, `NativeMathMl.TryRender` and
  `FastMathEngine.TryCompileToMathMl` (native first, managed fallback).
- `native/build-wasm.ps1` —Emscripten 3.1.56 `-O3` build of the same source to
  `fastmath.wasm` (24,854–25,051 B) plus `fastmath.js`.
- `npm/velocitex/src/fastmath.mjs` —reusable input/output buffers and UTF-8
  marshalling for the WASM module.
- `npm/velocitex/src/index.mjs` —fast path runs before `init()`; a supported
  formula does not download or start the .NET runtime. `fastMathml:false` forces
  the exact C# SVG/MathML contract; unsupported formulas fall back automatically.
- The supported subset covers the frozen corpus: atoms, digits, groups,
  fractions, radicals, scripts, text, operators, Greek letters, delimiters,
  `aligned`, matrix environments, `\left`/`\right`, accents, phantom and boxes.
  Anything else returns unsupported.

## Native AOT P/Invoke

Five independent Native AOT processes, 3,000 iterations over the 60-formula
corpus (180,000 calls per process). `NativeMathMl.TryRender`
includes UTF-8 encoding, P/Invoke, native parse/serialize and string allocation.

| Run | Mean µs | P50 µs | P95 µs | Managed bytes/call |
| --- | --- | --- | --- | --- |
| 1 | 3.203 | 2.9 | 6.4 | 916 |
| 2 | 3.099 | 2.5 | 6.3 | 916 |
| 3 | 3.284 | 2.9 | 6.4 | 916 |
| 4 | 3.185 | 2.7 | 6.4 | 916 |
| 5 | 3.781 | 3.0 | 6.5 | 916 |

KaTeX 0.19.0 MathML `renderToString` mean is 53.527 µs (P50 45 µs, P95 110 µs)
from the five-run native record. This final AOT run was taken while the
development VM was shared; worst C++ mean 3.781 µs → **14.2x**, best 3.099 µs →
**17.3x**. An earlier idle run measured 2.397–2.611 µs (20.5x–22.3x); the
conservative final numbers are used here. The 3x target requires at most 17.8 µs.

A standalone C++ loop over the same corpus measured 2.13 µs mean / 2.1 µs P50
(600,000 calls), so UTF-8 + P/Invoke + allocation costs about 0.3 µs.

## WASM C++ fast path

Final run: Chrome 128 headless, COOP/COEP (~5 µs timer), 300 iterations per
formula, five page rounds. KaTeX is the same 0.19.0 package and same corpus.

| Metric | VelociTex C++ WASM | KaTeX | Ratio |
| --- | --- | --- | --- |
| Parse →MathML P50 / mean | **0.010 / 0.0119 ms** | 0.040 / 0.0464 ms | **4.0x / 3.9x** |
| Parse →HTML P50 / mean | 0.010 / 0.0119 ms | 0.150 / 0.179 ms | 15.0x / 15.1x |
| Complex subset P50 | **0.010 ms** | 0.140 ms | **14x** |
| Single DOM sync P50 | **0.665 ms** | 2.310 ms | **3.47x** |
| Single DOM paint P50 | 33.30 ms | 33.20 ms | frame-bound, not claimed |
| 100-formula page sync | **37.8 ms** | 180.0 ms | **4.76x** |
| 100-formula page paint | **50.3 ms** | 232.8 ms | **4.63x** |

The 50% WASM target requires only 1.5x; the slowest claimed metric here is
3.47x. C# SVG parsing remains available (`output:'svg'`, `fastMathml:false`) and
was not used for the WASM claim because the C++ pre-render path emits MathML.

## Correctness and contract

- All 60 corpus formulas produce well-formed XML (lxml parse) from the native
  parser; 60/60 are supported.
- Browser parity suite with `fastMathml:false`: **3,630 valid + 79 diagnostic
  cases** pass unchanged, including exact SVG and MathML comparison.
- Fast-path assertions in the same browser run: native MathML contains
  `<mfrac>`, default fast HTML contains `<math>`, and an unsupported
  `\color{red}{x}` falls back to a C# SVG result.
- P1/P2/P4 JIT and Native AOT regression suites still pass (the C# engine was
  not changed by the C++ path).
- Native AOT fallback probe: native `x^2` and managed `\color{red}{x}` both
  compile.
- Isolated npm package check: `velocitex-0.1.0-preview.2.tgz` SHA-256
  `fba357a317a5e8128d5a253679a11b40e1aedf1b636068fcfa3a4e0b0cbcd7c6`; fresh
  install, copied runtime, strict TypeScript, 3,630 parity and the fast-path
  assertions all pass. This is a local package, not a publication.
- Visual inspection: `docs/verification/performance-fast-mathml.png` shows the
  100-formula page rendered by the C++ MathML path.

Contract changes, explicitly requested:

- Fast `html`/`mathml` output is native MathML without a source annotation; it is
  not the C# SVG DOM. Set `fastMathml:false` for the exact C# output.
- Browser-native MathML uses the browser's math font and layout, so glyph shapes,
  spacing and alignment differ from KaTeX. The screenshot records this.
- The fast path is a bounded subset. Unsupported constructs load the .NET runtime
  and use the full C# engine; no partial or silently wrong result is returned.
- C++ layout for the SVG/WinUI drawing pipelines was not implemented; those
  paths still use the C# layout engine and already meet their targets.

## Reproduction

```powershell
# Native parser and WASM module
./native/build-native.ps1
./native/build-wasm.ps1

# Native AOT P/Invoke benchmark (five runs)
dotnet publish benchmarks/VelociTex.NativeMath.Bench/VelociTex.NativeMath.Bench.csproj `
  -c Release -r win-x64 -p:PublishAot=true -o artifacts/native-bench-aot
$env:VELOCITEX_NATIVE_DIR = "$PWD/artifacts/native"
1..5 | % { ./artifacts/native-bench-aot/VelociTex.NativeMath.Bench.exe benchmarks/corpus.normal.json 3000 }

# Browser benchmark (uses fastMathml and native MathML for vt)
node benchmarks/wasm-browser/bench.mjs --out artifacts/performance/wasm-cpp --iterations 300 --page-rounds 5
```

Raw evidence is under ignored `artifacts/performance/native-cpp` and
`artifacts/performance/wasm-cpp-final3`.

## Remaining limits

- The fast parser is not a full TeX implementation. Macro definitions, color,
  cancel/covering commands and other rare constructs fall back to C#.
- Native MathML visual fidelity is browser/font dependent and intentionally
  different from KaTeX's SVG output.
- C++ layout/SVG for the native WinUI pipeline remains future work; the current
  WinUI target is met by the existing C# layout plus shared device resources.

