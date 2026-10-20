# VelociTex

VelociTex typesets complete TeX formulas using **KaTeX 0.19.0** mathematical rules.
It provides native Windows rendering, PNG / SVG / MathML export, a WinUI 3 control,
and an optional browser package. Native rendering uses .NET 10, DirectWrite and
Direct2D, without a browser or JavaScript runtime.

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
