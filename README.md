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

The measured throughput against KaTeX's own JavaScript renderer is reported by
`tests/wasm/katex-browser-benchmark.mjs`: the C++ WebAssembly core is
**3.8x faster** on the 2 core reference machine (recent readings:
3.73x–3.94x), which is
**short of the 5x target** of the project brief. The harness warms both
engines up for 25 rounds, measures 120 paired rounds and reports the median
of the per-round ratios, so the number does not depend on which round caught
the quietest moment. The **C++ core itself is
already about 5x faster than KaTeX when it runs natively** (26.6 µs versus
156–189 µs per formula in the browser on the same corpus, measured with the
same harness) —
the remaining gap is the WebAssembly and browser boundary, not the algorithm.
Quote the runtime of the KaTeX reference with every ratio: the same
`katex.mjs` runs ~40% slower in Node (232–288 µs per formula) than in
Chromium (156–244 µs), so the native core is 8.7x–10.8x against the Node
reading and 5.9x–7.1x against the Chromium one.
These are clang 20 numbers; the GCC 11.4 build of the same source is
12% slower (32.2 µs per formula, same harness and corpus),
and the WebAssembly build uses clang — so the WebAssembly tax,
measured against a like-for-like native build, is +44% and sits entirely in
the renderer, not in the parser.
The browser path is `native core x 1.37 (the WebAssembly tax) x 1.11-1.19
(the browser over Node) + 6 µs (the JavaScript layer)`, and the JavaScript
layer is only 12–15% of the whole path (measured: the public API against a
bare `vt_render` is 1.146), so **5x in the browser needs a native core of
about 16–20 µs** — 32–45% below the current one, and more than a rewrite of
all three stages delivers (that lands at ~4.5x, ~4.7x if the rewrite also
brings the WebAssembly tax down, and ~5.4x if the JavaScript layer is down to
the decode alone). `docs/benchmarks.md` has the
factors, the current numbers, the gap analysis and the optimization roadmap.

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
