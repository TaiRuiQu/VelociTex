# VelociTex

VelociTex compiles one complete TeX formula to independently owned native drawing
data. The compatibility baseline is KaTeX 0.19.0. The runtime uses C#/.NET 10 and
does not execute JavaScript, Node.js, a browser, WebView, or Typst.

The three packages are `VelociTex.Core`, `VelociTex.Rendering.Direct2D`, and
`VelociTex.WinUI`. Windows rendering supports Windows x64; WinUI applications
must supply their own normal Windows App SDK host configuration. Core is
platform-neutral and exports MathML. Direct2D exports transparent PNG and
self-contained SVG; MathView displays a precompiled result on a native
Composition surface and supports Control+C and read-only UI Automation values.

Local previews and release candidates are verified against their exact phase
records. A package version does not assert old-CPU performance acceptance or a
production release. Unsupported syntax
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
policy and retain its original text when compilation fails. Ordered painting
retains formula colors and transforms. Optional Windows contextual text shaping
runs inside compilation and retains independently owned outlines. The original
source, shaped text provenance and strict warnings remain available to hosts.

`CompileForPresentation` optionally draws ordinary syntax errors as literal
colored original source. `TryCompile` continues to return a structured
diagnostic. Caller shaping providers and strict handlers are trusted .NET code;
the compiler does not execute caller-defined TeX callbacks or external scripts.

VelociTex code is MIT. KaTeX code and generated rules retain the separate KaTeX
MIT notice. The unchanged embedded KaTeX fonts are **SIL OFL 1.1**, with Reserved
Font Names, and are **not MIT**. All texts ship in this package under `licenses/`
and `THIRD-PARTY-NOTICES.md`. `SECURITY.md` states the input and resource boundary.
No external publication or source push is implied by local packing.
