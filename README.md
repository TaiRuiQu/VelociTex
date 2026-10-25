# VelociTex

English | [中文](README.zh-CN.md)

VelociTex typesets complete TeX formulas using **KaTeX 0.19.0** mathematical rules.
It provides native Windows rendering, PNG / SVG / MathML export, a WinUI 3 control,
and an optional browser package. A native C++ typesetting core runs through .NET 10 bindings, DirectWrite and
Direct2D, without a browser or JavaScript runtime.

<!-- benchmark:start -->

## One C++ core, two endpoints

There is exactly **one** formula parser and renderer in this repository: the
C++ core in `native/core`. Both public endpoints call it, and nothing else
parses formulas.

| Endpoint                  | Reaches the core through                         |
| ------------------------- | ------------------------------------------------ |
| .NET (`VelociTex.Core`)   | the C ABI in `native/core/api.cpp`               |
| Browser (npm `velocitex`) | `native/core`, compiled to `velocitex-core.wasm` |

The browser package ships no JavaScript formula parser and no Rust formula
parser: the WebAssembly module is the C++ core, and its HTML and MathML are
produced by the same code the .NET endpoint uses.

## KaTeX-identical browser HTML

The npm package's default browser path emits the same HTML and MathML markup
as KaTeX 0.19.0 for the supported core formula set, with no
MathML-as-rendering shortcut. It uses the locked KaTeX 0.19.0 stylesheet and the
unchanged KaTeX fonts. The native parity gate compares **5,411 formulas**
byte for byte against KaTeX — 1,605 real world formulas, the 60 formula curated
feature corpus, the 36 frozen benchmark workloads and the repository's own
`tests/fixtures/core.json` (3,710 registered symbols, functions, macros and
environments) — for **both** the HTML and the MathML output, and the browser
gate repeats 1,651 of them (the real world corpus, the benchmark workloads and
the curated samples) inside Chromium.

The browser package renders **3.9x faster than KaTeX 0.19.0** in Chromium on
the 36-formula benchmark: **about 43 µs vs 165 µs per formula** (three-run
median, 120 paired rounds). The native C++ core renders the same corpus in
**29.2 µs per formula** — about 5.7x faster than KaTeX in the browser and 9.9x
faster than KaTeX in Node. All 36 benchmark formulas are byte identical to
KaTeX before timing. See [the benchmark guide](docs/benchmarks.md) for the full
tables, environment and reproduction steps.

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
Use the version available from your package feed; the stable npm release uses `latest`.

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
npm install velocitex@1.0.0
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
