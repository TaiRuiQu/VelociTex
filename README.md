# VelociTex

English | [中文](README.zh-CN.md)

VelociTex typesets complete TeX formulas using **KaTeX 0.19.0** mathematical rules.
It provides native Windows rendering, PNG / SVG / MathML export, a WinUI 3 control,
and an optional browser package. A native C++ typesetting core runs through .NET 10 bindings, DirectWrite and
Direct2D, without a browser or JavaScript runtime.

<!-- benchmark:start -->

## Performance

C++ WASM + fallback delivers **1.6–3.1× KaTeX browser render-work throughput** across these three workloads. The native C++ Core generates common-formula MathML in **24.6 µs/formula (~40,667 formulas/s)** on this machine, without a window or refresh-rate limit.

2026-10-20 · Windows x64 · Release · KaTeX 0.19.0 · µs/formula (lower is better).

| MathML generation | Native C++ ABI | C++ WASM + fallback | KaTeX | WASM relative throughput |
| ----------------- | -------------: | ------------------: | ----: | -----------------------: |
| Common formulas   |           24.6 |                 7.7 |  14.0 |                    1.81× |
| Complex formulas  |          208.6 |               137.4 | 117.6 |                    0.86× |
| Mixed pages       |           20.2 |                 3.7 |  11.7 |                    3.18× |

| Render work      | Native WinUI / Direct2D | WASM DOM + layout | KaTeX DOM + layout |
| ---------------- | ----------------------: | ----------------: | -----------------: |
| Common formulas  |                  1174.7 |             248.8 |              644.4 |
| Complex formulas |                 16228.2 |            2684.3 |             4196.5 |
| Mixed pages      |                   368.8 |             179.5 |              561.6 |

The complex-formula MathML workload takes 1.17× KaTeX time in WASM because fallback remains included. Native WinUI is slower than KaTeX on single/complex render work; its window/surface diagnostics can include graphics-queue backpressure and are separate from pure C++ engine performance. There is no claim of a universal speedup.

![Render-work throughput benchmark](docs/benchmarks/benchmark-render-work.png)

All 36 formula occurrences are included, including automatic fallback. Three fresh processes × 15 calibrated batches per case; medians of batch averages, with actual page formula counts. No explicit frame waits or document cache. Startup/download excluded. Render work measures native surface submission or browser DOM + forced layout, with different output backends; it excludes a GPU completion fence, browser rasterization, and screen presentation. Results are specific to this hardware and corpus.

[Methodology, all cases, raw batches and reproduction commands](benchmarks/README.md).

## Rendering comparison

Actual output at 32 px math em, captured at 2× resolution. Each row uses equal-scale canvases; table display scales them together. WASM uses the default C++ MathML route with automatic full-Core fallback. Native and KaTeX use their own font rendering; identical pixels are not claimed.

| Formula                | Windows native / Direct2D                                                                                  | VelociTex WASM                                                                                         | KaTeX 0.19.0                                                                                             |
| ---------------------- | ---------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------- |
| Algebra                | <img src="docs/benchmarks/rendering/algebra-native.png" width="280" alt="Algebra: native">                 | <img src="docs/benchmarks/rendering/algebra-wasm.png" width="280" alt="Algebra: wasm">                 | <img src="docs/benchmarks/rendering/algebra-katex.png" width="280" alt="Algebra: katex">                 |
| Calculus               | <img src="docs/benchmarks/rendering/calculus-native.png" width="280" alt="Calculus: native">               | <img src="docs/benchmarks/rendering/calculus-wasm.png" width="280" alt="Calculus: wasm">               | <img src="docs/benchmarks/rendering/calculus-katex.png" width="280" alt="Calculus: katex">               |
| Piecewise & matrices   | <img src="docs/benchmarks/rendering/structures-native.png" width="280" alt="Piecewise & matrices: native"> | <img src="docs/benchmarks/rendering/structures-wasm.png" width="280" alt="Piecewise & matrices: wasm"> | <img src="docs/benchmarks/rendering/structures-katex.png" width="280" alt="Piecewise & matrices: katex"> |
| Color, accents & fonts | <img src="docs/benchmarks/rendering/color-native.png" width="280" alt="Color, accents & fonts: native">    | <img src="docs/benchmarks/rendering/color-wasm.png" width="280" alt="Color, accents & fonts: wasm">    | <img src="docs/benchmarks/rendering/color-katex.png" width="280" alt="Color, accents & fonts: katex">    |

<!-- benchmark:end -->

## Choose a package

| Package                        | Use                                                                 |
| ------------------------------ | ------------------------------------------------------------------- |
| `VelociTex.Core`               | Compile formulas, inspect dimensions and diagnostics, export MathML |
| `VelociTex.Rendering.Direct2D` | Render on Windows x64 and export PNG / SVG                          |
| `VelociTex.WinUI`              | Display formulas with the native `MathView` control                 |
| npm `velocitex`                | Render formulas in a browser, with TypeScript declarations          |

The source distribution also includes a portable SVG backend and an optional
C++ MathML adapter.

Native packages require .NET 10. Windows rendering targets Windows 10 build
19041 or later, x64. WinUI apps supply their own Windows App SDK host. Native AOT
and trimming are supported. Browser rendering ships its own WebAssembly assets.
Use the version available from your package feed; the npm preview uses `next`.

## Compile and export

```csharp
using VelociTex;
using VelociTex.Rendering.Direct2D;

var engine = new MathEngine();
using var workspace = new MathWorkspace();
var options = new MathOptions { FontSize = 24, DisplayMode = true };
if (engine.TryCompile(@"\frac{a}{b}+\sqrt{x}", in options, workspace,
    CancellationToken.None, out var document, out var diagnostic))
{
    using var renderer = new WindowsMathRenderer();
    renderer.WritePng(document!, "formula.png");
    renderer.WriteSvg(document!, "formula.svg");
    File.WriteAllText("formula.mathml", MathMlWriter.Write(document!));
}
else
{
    Console.WriteLine($"{diagnostic.Code}: {diagnostic.Message}");
}
```

## Browser

```sh
npm install velocitex@next
npx velocitex-copy-assets public/velocitex
```

```js
import { init, renderAll } from 'velocitex';
await init({ assetBaseUrl: '/velocitex/' });
await renderAll();
```

## Documentation

- [Native usage](docs/native-usage.md) · [中文](docs/native-usage.zh-CN.md)
- [Benchmarks](docs/benchmarks.md) · methodology and charts
- [API, ownership and resource limits](docs/package-contract.md)
- [Browser API and deployment](npm/velocitex/README.md) · [中文](docs/wasm-js.zh-CN.md)
- [Export example](samples/VelociTex.Export.Sample/README.md)
- [Browser example](samples/VelociTex.Web.Sample/README.md)
- [Input safety](SECURITY.md) · [Third-party licenses](THIRD-PARTY-NOTICES.md)

VelociTex handles one complete formula at a time. It does not compile full LaTeX
documents, load arbitrary TeX packages, edit formulas, or wrap surrounding body
text. Unsupported syntax returns diagnostics. Browser font fallback is limited;
see the browser guide before using text outside the bundled mathematical fonts.

Code is MIT. The bundled KaTeX fonts and extracted contours retain **OFL-1.1**
and Reserved Font Name terms; the fonts are not MIT.
