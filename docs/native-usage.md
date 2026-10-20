# Native usage

English | [中文](native-usage.zh-CN.md)

Use .NET 10. Add `VelociTex.Core` and, for Windows rendering,
`VelociTex.Rendering.Direct2D` from your package feed. Add `VelociTex.WinUI` to
use the control in an existing WinUI 3 application. Windows rendering targets
Windows 10 build 19041 or later, x64.

## Compile a formula

```csharp
using VelociTex;

var engine = new MathEngine();
using var workspace = new MathWorkspace();
var options = new MathOptions { FontSize = 24, DisplayMode = true };
if (!engine.TryCompile(@"\int_0^1 x^2,dx", in options, workspace,
    CancellationToken.None, out var result, out var diagnostic))
{
    Console.WriteLine($"{diagnostic.Code}: {diagnostic.Message}");
    return;
}
Console.WriteLine($"{result!.LogicalSize}; baseline={result.Baseline}");
File.WriteAllText("formula.mathml", MathMlWriter.Write(result));
```

Input is TeX without outer `$` delimiters. `DisplayMode` chooses display-style
mathematics. `FontSize`, dimensions and baseline use device-independent pixels.
Reuse a workspace serially; each result owns its data and survives workspace
reuse or disposal. Use independent workspaces for concurrent compilations.

Pass string definitions through `MathOptions.Macros`. Definitions are local to
each formula unless you explicitly supply a `MacroSession`; see the
[API contract](package-contract.md) for session lifetime and rollback behavior.

## Windows images and fallback text

```csharp
using VelociTex.Rendering.Direct2D;

using var renderer = new WindowsMathRenderer();
renderer.WritePng(result!, "formula.png");
renderer.WriteSvg(result!, "formula.svg");
```

The renderer loads bundled fonts privately. Dispose renderers and prepared
resources on their creating thread. Stream overloads leave caller streams open.
To shape text outside the bundled fonts, set `MathOptions.TextShaper` to a
`WindowsMathTextShaper` before compilation. Availability depends on installed
Windows fonts. SVG includes outlines and can be viewed without installing fonts.

## WinUI 3

Declare the control in your application's XAML:

```xml
<math:MathView x:Name="Formula" xmlns:math="using:VelociTex.WinUI" />
```

After successful compilation, assign the result on the UI thread:

```csharp
Formula.Result = result;
Formula.AccessibleDescription = "Integral from zero to one of x squared";
```

Assign `null` to clear it. Colors follow the theme unless `FormulaColor` is set;
call `UseThemeColor()` to restore this behavior. The control supports whole-source
copying with Ctrl+C and exposes the source as a read-only UI Automation value.
Provide an accessible description appropriate for your application. Structural
spoken-math navigation and partial formula selection are not provided.

## MathML without drawing

`MathEngine.TryCompileToMathMl` skips layout and returns the same managed MathML
as `MathMlWriter.Write` for equivalent input and options. The optional
`VelociTex.NativeMath` adapter tries C++ presentation MathML and falls back to
the managed engine. Deploy `VelociTex.NativeMath.dll` beside your Windows app
or use `VELOCITEX_NATIVE_DIR` to locate it. Native output may differ in markup
and omits the source annotation; use the managed API when exact output matters.

## Errors and limits

`TryCompile` returns `false` and a diagnostic for invalid or unsupported input.
Keep the original source available to the user. Diagnostic ranges are UTF-16
offsets. `CompileForPresentation` can display ordinary syntax errors as colored
literal source. Configure input, expansion, node and nesting limits through
`MathOptions`; cancellation is cooperative. PNG exports are limited to 8,192
pixels per side and 4,194,304 pixels total.

See [resource and threading details](package-contract.md),
[input safety](../SECURITY.md) and the
[runnable export example](../samples/VelociTex.Export.Sample/README.md).
