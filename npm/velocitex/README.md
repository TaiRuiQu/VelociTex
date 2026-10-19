# VelociTex for the web

Render math with the existing VelociTex C# engine, compiled to WebAssembly.
The compatibility baseline is **KaTeX 0.19.0 core math**. The API is inspired by
KaTeX, with asynchronous initialization. It is not a drop-in replacement for
all KaTeX options or extensions. This is an early browser preview.

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
installed .NET is needed. The browser downloads the bundled .NET WASM runtime.

## Rendering examples

The same formulas are shown at the same 32-pixel math em size and foreground
color. Each image compares actual **Windows native DirectWrite/Direct2D output**,
the **VelociTex WASM binding**, and **KaTeX 0.19.0**. The examples cover algebra,
calculus, matrices, aligned equations, colors, accents and mathematical fonts.

### Algebra and nested fractions

![Algebra: Windows native, VelociTex WASM and KaTeX 0.19.0](https://cdn.jsdelivr.net/npm/velocitex@0.1.0-preview.2/images/comparison-algebra.png)

### Calculus

![Calculus: Windows native, VelociTex WASM and KaTeX 0.19.0](https://cdn.jsdelivr.net/npm/velocitex@0.1.0-preview.2/images/comparison-calculus.png)

### Matrices, piecewise functions and aligned equations

![Structures: Windows native, VelociTex WASM and KaTeX 0.19.0](https://cdn.jsdelivr.net/npm/velocitex@0.1.0-preview.2/images/comparison-structures.png)

### Dark theme, colors, fonts and decorations

![Dark theme: Windows native, VelociTex WASM and KaTeX 0.19.0](https://cdn.jsdelivr.net/npm/velocitex@0.1.0-preview.2/images/comparison-dark.png)

These 16 examples show closely matching mathematical layout, with visible
antialiasing and minor subpixel differences. They do not establish pixel identity
or compatibility for every possible input. The original comparison PNGs are
included in the package's `images/` directory; known font limitations are below.

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
  hidden semantic MathML). `output: 'svg' | 'mathml'` selects one standalone format.
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
or screen-reader audio acceptance claim. Source is retained as
`data-velocitex-source`; partial visual text selection is not implemented.

**Font fallback limitation:** mathematical glyphs in the 20 locked KaTeX fonts
are supported. Windows DirectWrite is unavailable in a browser. Chinese text,
other missing glyphs and contextual scripts requiring a platform shaper return
`UnknownSymbol`, or readable original source when `throwOnError: false`. Full
Windows fallback compatibility is not claimed by this preview.

The package uses a Release WASM AOT build. End users need neither the SDK nor
Node.js in the browser. It carries the .NET runtime; download size, first-use
latency and warm rendering are separate costs. No speedup over KaTeX is asserted.

Code is MIT. Bundled unchanged KaTeX fonts and their extracted contours retain
OFL-1.1 and Reserved Font Name terms; see `licenses/KaTeX-FONTS-OFL.txt`. .NET
runtime third-party notices are retained with the distribution.
