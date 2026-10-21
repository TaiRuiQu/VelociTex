# VelociTex

VelociTex typesets complete TeX formulas using **KaTeX 0.19.0** mathematical rules.
It provides native Windows rendering, PNG / SVG / MathML export, a WinUI 3 control,
and an optional browser package. A native C++ typesetting core runs through .NET 10 bindings, DirectWrite and
Direct2D, without a browser or JavaScript runtime.

<!-- benchmark:start -->

## Performance

2026-10-21 · Windows x64 · Release · KaTeX 0.19.0 · µs/formula (lower is better).

Both browser engines generate default HTML with accessibility MathML, using the same locked KaTeX CSS and fonts. Every benchmark formula is checked for byte-identical markup before timing.

Correctness validation passed for 3,659 valid core HTML/MathML fixtures and 46 browser integration formulas, including bundled stylesheet and font loading.

| Workload         | WASM HTML generation | KaTeX HTML generation | WASM DOM + layout | KaTeX DOM + layout |
| ---------------- | -------------------: | --------------------: | ----------------: | -----------------: |
| Common formulas  |                 43.4 |                  44.5 |             615.4 |              593.4 |
| Complex formulas |                426.2 |                 431.8 |            3649.2 |             3538.9 |
| Mixed pages      |                 44.9 |                  40.7 |             556.1 |              514.4 |

WASM/KaTeX relative throughput is **0.91–1.03×** for HTML generation and **0.93–0.97×** for DOM render work on this machine. Ratios below 1 mean WASM is slower.

![HTML generation](https://raw.githubusercontent.com/TaiRuiQu/VelociTex/v1.0.0-preview7/docs/benchmarks/benchmark-comparison.png)

![Browser render work](https://raw.githubusercontent.com/TaiRuiQu/VelociTex/v1.0.0-preview7/docs/benchmarks/benchmark-render-work.png)

All 36 formula occurrences are retained: 6 common, 3 complex, and 27 across three mixed pages. Three fresh processes × 15 calibrated batches per case; each case warms for at least 200 ms. Public async WASM API overhead is included. DOM timing forces layout after every formula. Startup, download, browser rasterization, GPU completion and screen presentation are excluded. Native C++/.NET/WinUI results and MathML serialization are separate diagnostics; the C++ Core does not expose this HTML generator.

[Methodology and reproduction](https://github.com/TaiRuiQu/VelociTex/blob/v1.0.0-preview7/benchmarks/README.md).

## Rendering comparison

Actual output at 32 px math em and 2× capture resolution. The WASM column uses the current default HTML/CSS renderer and bundled fonts. All four previews have matching computed styles and dimensions in their visible HTML trees. Three have byte-identical markup; the combined color example differs in CSS declaration order and a redundant inherited color declaration. Captures are not asserted pixel-identical. Cells in each row share a canvas and scale. Native uses DirectWrite/Direct2D.

| Formula                | Windows native / Direct2D                                                                                                                                                       | VelociTex HTML/CSS WASM                                                                                                                                                     | KaTeX 0.19.0                                                                                                                                                                  |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Algebra                | <img src="https://raw.githubusercontent.com/TaiRuiQu/VelociTex/v1.0.0-preview7/docs/benchmarks/rendering/algebra-native.png" width="280" alt="Algebra: native">                 | <img src="https://raw.githubusercontent.com/TaiRuiQu/VelociTex/v1.0.0-preview7/docs/benchmarks/rendering/algebra-wasm.png" width="280" alt="Algebra: wasm">                 | <img src="https://raw.githubusercontent.com/TaiRuiQu/VelociTex/v1.0.0-preview7/docs/benchmarks/rendering/algebra-katex.png" width="280" alt="Algebra: katex">                 |
| Calculus               | <img src="https://raw.githubusercontent.com/TaiRuiQu/VelociTex/v1.0.0-preview7/docs/benchmarks/rendering/calculus-native.png" width="280" alt="Calculus: native">               | <img src="https://raw.githubusercontent.com/TaiRuiQu/VelociTex/v1.0.0-preview7/docs/benchmarks/rendering/calculus-wasm.png" width="280" alt="Calculus: wasm">               | <img src="https://raw.githubusercontent.com/TaiRuiQu/VelociTex/v1.0.0-preview7/docs/benchmarks/rendering/calculus-katex.png" width="280" alt="Calculus: katex">               |
| Piecewise & matrices   | <img src="https://raw.githubusercontent.com/TaiRuiQu/VelociTex/v1.0.0-preview7/docs/benchmarks/rendering/structures-native.png" width="280" alt="Piecewise & matrices: native"> | <img src="https://raw.githubusercontent.com/TaiRuiQu/VelociTex/v1.0.0-preview7/docs/benchmarks/rendering/structures-wasm.png" width="280" alt="Piecewise & matrices: wasm"> | <img src="https://raw.githubusercontent.com/TaiRuiQu/VelociTex/v1.0.0-preview7/docs/benchmarks/rendering/structures-katex.png" width="280" alt="Piecewise & matrices: katex"> |
| Color, accents & fonts | <img src="https://raw.githubusercontent.com/TaiRuiQu/VelociTex/v1.0.0-preview7/docs/benchmarks/rendering/color-native.png" width="280" alt="Color, accents & fonts: native">    | <img src="https://raw.githubusercontent.com/TaiRuiQu/VelociTex/v1.0.0-preview7/docs/benchmarks/rendering/color-wasm.png" width="280" alt="Color, accents & fonts: wasm">    | <img src="https://raw.githubusercontent.com/TaiRuiQu/VelociTex/v1.0.0-preview7/docs/benchmarks/rendering/color-katex.png" width="280" alt="Color, accents & fonts: katex">    |

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

See `docs/package-contract.md` in this package for ownership, threading and
resource limits. [Online documentation](https://github.com/TaiRuiQu/VelociTex)
also covers WinUI, browser use and examples.

Code is MIT. Bundled KaTeX fonts retain OFL-1.1 and Reserved Font Name terms;
see `THIRD-PARTY-NOTICES.md` and `licenses/`.
