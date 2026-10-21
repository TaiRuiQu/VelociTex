# VelociTex

English | [中文](README.zh-CN.md)

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

The default path emits KaTeX-identical HTML and uses the bundled KaTeX
stylesheet/fonts; the stylesheet is injected automatically. `output: 'svg'` or
`fastHtml: false` selects the full-Core SVG contract instead.

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
text. Unsupported syntax returns diagnostics. The KaTeX-compatible HTML path matches
KaTeX's browser font fallback; the legacy SVG path has no browser DirectWrite
shaper, so see the browser guide before using text outside the bundled fonts there.

Code is MIT. The bundled KaTeX fonts and extracted contours retain **OFL-1.1**
and Reserved Font Name terms; the fonts are not MIT.
