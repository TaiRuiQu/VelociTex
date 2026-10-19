# VelociTex local preview

VelociTex compiles one complete TeX formula to independently owned native drawing
data. The compatibility baseline is KaTeX 0.19.0. The runtime uses C#/.NET 10 and
does not execute JavaScript, Node.js, a browser, WebView, or Typst.

The three packages are `VelociTex.Core`, `VelociTex.Rendering.Direct2D`, and
`VelociTex.WinUI`. Windows rendering supports Windows x64; WinUI applications
must supply their own normal Windows App SDK host configuration. Core is
platform-neutral and exports MathML. Direct2D exports transparent PNG and
self-contained SVG; MathView displays a precompiled result on a native
Composition surface and supports Control+C and read-only UI Automation values.

This is a local, unlisted preview. It does not claim complete core compatibility,
old-CPU performance acceptance, or a production release. Unsupported syntax
returns a structured diagnostic. Follow the repository phase records for tested
commands, limits, visual differences and unverified scenarios.

```csharp
using VelociTex;
using VelociTex.Rendering.Direct2D;

var engine = new MathEngine();
using var workspace = new MathWorkspace();
var options = new MathOptions { FontSize = 24, DisplayMode = true };
if (engine.TryCompile(@"\frac{a}{b}+\sqrt{x}", in options, workspace,
    CancellationToken.None, out var result, out var diagnostic))
{
    using var renderer = new WindowsMathRenderer(); // Windows only, same thread
    renderer.WritePng(result!, "formula.png");
    renderer.WriteSvg(result!, "formula.svg");
    File.WriteAllText("formula.mathml", MathMlWriter.Write(result!));
}
else
{
    Console.WriteLine($"{diagnostic.Code}: {diagnostic.Message}");
}
```

A workspace is serial and reusable; results do not borrow its buffers. Renderer,
prepared native resources and MathView are confined to their creating thread.
Dispose native resources. Input, expansion, nesting, node and export-pixel limits
are bounded; treat untrusted input with an application-appropriate cancellation
policy and retain its original text when compilation fails.

VelociTex code is MIT. KaTeX code and generated rules retain the separate KaTeX
MIT notice. The unchanged embedded KaTeX fonts are **SIL OFL 1.1**, with Reserved
Font Names, and are **not MIT**. All texts ship in this package under `licenses/`
and `THIRD-PARTY-NOTICES.md`. No external publication is implied by local packing.
