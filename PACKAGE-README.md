# VelociTex

VelociTex typesets complete TeX formulas using **KaTeX 0.19.0** mathematical rules.
It provides native Windows rendering, PNG / SVG / MathML export, a WinUI 3 control,
and an optional browser package. A native C++ typesetting core runs through .NET 10 bindings, DirectWrite and
Direct2D, without a browser or JavaScript runtime.

<!-- benchmark:start -->

## KaTeX-identical browser HTML

The npm package's default browser path is a standalone WebAssembly engine that
emits the same HTML and MathML markup as KaTeX 0.19.0 for the supported core
formula set, with no MathML-as-rendering shortcut. It uses the locked KaTeX 0.19.0 stylesheet and the
unchanged KaTeX fonts. The parity gate covers all **3,659 valid core fixtures**
(byte-identical HTML and MathML) plus the browser integration suite.

The engine remains a native WebAssembly fast path: generation is measured at
roughly **0.8–2.2× KaTeX API throughput** across six representative formulas in
Chromium on the development machine, depending on formula shape and hardware.
This replaces the earlier fast MathML path; native Direct2D/WinUI performance is
unchanged and documented separately in `benchmarks/README.md`.

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
