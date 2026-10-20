# VelociTex for the web

Render math with WebAssembly: a compact C++ MathML path and the full native C++ Core through .NET WASM bindings.
The compatibility baseline is **KaTeX 0.19.0 core math**. The API is inspired by
KaTeX, with asynchronous initialization. It is not a drop-in replacement for
all KaTeX options or extensions. This is an early browser preview.

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

![Render-work throughput benchmark](https://cdn.jsdelivr.net/npm/velocitex@1.0.0-preview6/images/benchmark-render-work.png)

All 36 formula occurrences are included, including automatic fallback. Three fresh processes × 15 calibrated batches per case; medians of batch averages, with actual page formula counts. No explicit frame waits or document cache. Startup/download excluded. Render work measures native surface submission or browser DOM + forced layout, with different output backends; it excludes a GPU completion fence, browser rasterization, and screen presentation. Results are specific to this hardware and corpus.

[Methodology, all cases, raw batches and reproduction commands](https://github.com/TaiRuiQu/VelociTex/blob/v1.0.0-preview6/benchmarks/README.md).

## Rendering comparison

Actual output at 32 px math em, captured at 2× resolution. Each row uses equal-scale canvases; table display scales them together. WASM uses the default C++ MathML route with automatic full-Core fallback. Native and KaTeX use their own font rendering; identical pixels are not claimed.

| Formula                | Windows native / Direct2D                                                                                                                               | VelociTex WASM                                                                                                                                      | KaTeX 0.19.0                                                                                                                                          |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| Algebra                | <img src="https://cdn.jsdelivr.net/npm/velocitex@1.0.0-preview6/images/rendering/algebra-native.png" width="280" alt="Algebra: native">                 | <img src="https://cdn.jsdelivr.net/npm/velocitex@1.0.0-preview6/images/rendering/algebra-wasm.png" width="280" alt="Algebra: wasm">                 | <img src="https://cdn.jsdelivr.net/npm/velocitex@1.0.0-preview6/images/rendering/algebra-katex.png" width="280" alt="Algebra: katex">                 |
| Calculus               | <img src="https://cdn.jsdelivr.net/npm/velocitex@1.0.0-preview6/images/rendering/calculus-native.png" width="280" alt="Calculus: native">               | <img src="https://cdn.jsdelivr.net/npm/velocitex@1.0.0-preview6/images/rendering/calculus-wasm.png" width="280" alt="Calculus: wasm">               | <img src="https://cdn.jsdelivr.net/npm/velocitex@1.0.0-preview6/images/rendering/calculus-katex.png" width="280" alt="Calculus: katex">               |
| Piecewise & matrices   | <img src="https://cdn.jsdelivr.net/npm/velocitex@1.0.0-preview6/images/rendering/structures-native.png" width="280" alt="Piecewise & matrices: native"> | <img src="https://cdn.jsdelivr.net/npm/velocitex@1.0.0-preview6/images/rendering/structures-wasm.png" width="280" alt="Piecewise & matrices: wasm"> | <img src="https://cdn.jsdelivr.net/npm/velocitex@1.0.0-preview6/images/rendering/structures-katex.png" width="280" alt="Piecewise & matrices: katex"> |
| Color, accents & fonts | <img src="https://cdn.jsdelivr.net/npm/velocitex@1.0.0-preview6/images/rendering/color-native.png" width="280" alt="Color, accents & fonts: native">    | <img src="https://cdn.jsdelivr.net/npm/velocitex@1.0.0-preview6/images/rendering/color-wasm.png" width="280" alt="Color, accents & fonts: wasm">    | <img src="https://cdn.jsdelivr.net/npm/velocitex@1.0.0-preview6/images/rendering/color-katex.png" width="280" alt="Color, accents & fonts: katex">    |

<!-- benchmark:end -->

## Install and use

Install the browser preview from npm:

```sh
npm install velocitex@next
```

For Vite or another bundler, copy the runtime as static assets:

```sh
npx velocitex-copy-assets public/velocitex
```

```html
<p>Inline: \(E=mc^2\).</p>
<div data-math="\int_0^1 x^2\,dx" data-display-mode="true"></div>
```

```js
import { init, renderAll } from 'velocitex';

// Set this to your public assets URL, including any deployment subpath.
await init({ assetBaseUrl: '/velocitex/' });
await renderAll();
```

Copy assets again whenever you update the package. Keep dotnet.js and every
hashed sibling from the same build together. Serve `.wasm` as `application/wasm`
and `.js` as JavaScript. No COOP/COEP headers are required: this build uses one
thread. Use HTTP(S), not `file://`.

When serving the package's `dist/` directory directly, initialization finds the
adjacent `runtime/` automatically:

```html
<script type="module">
  import { renderAll } from './dist/index.mjs';
  await renderAll();
</script>
```

SVG contours are self-contained: no stylesheet, external font download or
installed .NET is needed. The C++ MathML fast path is a 24 KB WebAssembly module
and does not download the .NET runtime for supported formulas; unsupported
formulas or `fastMathml: false` fall back to the bundled .NET WASM runtime.

## Fast MathML path

`render` and `renderToString` use a C++ parser/compiler by default when `output`
is `html` or `mathml`. It performs the complete pre-render pipeline in a small
standalone WASM module and lets the browser lay out native MathML; for supported
formulas the .NET runtime is never started. This is much faster and produces
valid MathML, but glyph shapes and spacing follow the browser's math font rather
than KaTeX's SVG. Set `fastMathml: false` or `output: 'svg'` for the exact
full-Core SVG output. Unsupported commands automatically use that full
Core path, so correctness is preserved.

## APIs

```js
import velocitex, { render, renderToString, ParseError } from 'velocitex';

await render(String.raw`\frac{a}{b}`, document.querySelector('#formula'));
const html = await renderToString(String.raw`\sqrt{x}`, { displayMode: true });
const svg = await renderToString('x^2', { output: 'svg' });
const mathml = await renderToString('x^2', { output: 'mathml' });
const report = await velocitex.renderAll(document.querySelector('#article'));
console.log(report.rendered, report.errors);
```

- `init({ assetBaseUrl? })`: optional when the runtime is beside the ES module;
  shared by concurrent calls. Call before rendering to override the asset URL.
- `render(source, element, options?)`: replaces the element's contents.
- `renderToString(source, options?)`: returns HTML by default (SVG plus visually
  hidden semantic MathML, unless the native fast path handles it). `output: 'svg' |
'mathml'` selects one standalone format. `fastMathml: false` forces the exact
  full-Core SVG/MathML contract.
- `renderAll(root = document.body, options?)`: scans `[data-math]` elements and
  text containing `$$…$$`, `\[…\]`, or `\(…\)`. Repeated calls skip rendered
  output. `data-math` supplies TeX; a custom `selector` can use element text.
  `data-display-mode="true"` enables display math, `"false"` inline math.

`renderAll` skips code, pre, scripts, style, textareas, SVG, MathML,
contenteditable regions and `.velocitex-ignore`. Delimiters must be within a
single text node. Escaped delimiters and braces are respected. It does not
observe future DOM changes; call it again after adding new content. Single `$`
is opt-in to avoid interpreting currency:

```js
await renderAll(document.body, {
  delimiters: [
    { left: '$$', right: '$$', display: true },
    { left: '$', right: '$', display: false },
  ],
});
```

`render` and `renderToString` throw `ParseError` for invalid TeX by default.
Use `{ throwOnError: false }` to display safely escaped original source in the
error color. `renderAll` defaults to this behavior and returns structured errors
with `source`, `error.code`, `error.range` (UTF-16) and the affected element.
Infrastructure errors and invalid JS arguments still reject the Promise.

Supported options are declared in `index.d.ts`: `fontSize` (CSS pixels, default
24), `displayMode`, string `macros`, `strict`, `maxExpand`, input/node/token/nesting
limits, `maxSize`, `minRuleThickness`, `colorIsTextColor`, `leqno`, `fleqn`,
`displayWidth`, `output`, `throwOnError`, `errorColor`. Resource budgets remain
enforced by Core. `renderAll` additionally limits each call to 1,000 formulas
(configurable up to 10,000). Successful earlier formulas can remain if a later
formula exceeds this bound.

Macros are isolated per formula: no implicit cross-formula definition mutation
or JS callback macros. There is no `trust`, HTML injection, external images,
links, mhchem, body-text layout, automatic formula line breaking, Node.js SSR,
or structural spoken-math navigation. Source is retained as
`data-velocitex-source`; partial visual text selection is not implemented.

**Font fallback limitation:** mathematical glyphs in the 20 locked KaTeX fonts
are supported. Windows DirectWrite is unavailable in a browser. Chinese text,
other missing glyphs and contextual scripts requiring a platform shaper return
`UnknownSymbol`, or readable original source when `throwOnError: false`. Full
Windows fallback compatibility is not claimed by this preview.

Supported formulas use the standalone C++ WebAssembly MathML path. Other inputs
load the bundled .NET WASM AOT runtime. End users need neither the SDK nor Node.js
in the browser. Deploy all runtime assets together when upgrading the package.

Code is MIT. Bundled unchanged KaTeX fonts and their extracted contours retain
OFL-1.1 and Reserved Font Name terms; see `licenses/KaTeX-FONTS-OFL.txt`. .NET
runtime third-party notices are retained with the distribution.
