# VelociTex for the web

Render math with the VelociTex C++ core compiled to WebAssembly. It emits the
**same HTML and MathML as KaTeX 0.19.0** for the supported core formula set and
uses the locked KaTeX stylesheet and fonts. The API
is inspired by KaTeX, with asynchronous initialization. It is not a drop-in
replacement for all KaTeX options or extensions. This is an early browser preview.

<!-- benchmark:start -->

## KaTeX-identical output

The default `html` output is not native MathML and not a browser-math-font
approximation: the WebAssembly module produces KaTeX's `.katex` HTML, hidden
semantic MathML, inline styles and SVG fragments byte for byte. The module is
the VelociTex C++ core, the same code the .NET endpoint uses; the package
contains no separate JavaScript or Rust formula parser. The parity gate
compares **5,411 formulas** byte for byte against KaTeX 0.19.0 — in
`tests/native/katex-html-parity.mjs` and
`tests/native/katex-mathml-parity.mjs` natively, and in
`tests/wasm/katex-browser-parity.mjs` in Chromium.

The bundled stylesheet is the unchanged KaTeX 0.19.0 `katex.min.css`, together
with the unchanged KaTeX fonts. The fast path injects that stylesheet on first
use; callers can also link it manually. The throughput against KaTeX's own
JavaScript renderer is measured by
`tests/wasm/katex-browser-benchmark.mjs`; the current numbers and the
methodology live in `benchmarks/README.md`.

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

The WebAssembly module uses the non-trapping float to int conversion of the
WebAssembly specification, so it needs a browser of 2021 or later (Chrome 74,
Firefox 62, Safari 15). An older browser cannot instantiate the module; the
package reports that it is unavailable and the .NET runtime path takes over.

When serving the package's `dist/` directory directly, initialization finds the
adjacent `runtime/` automatically:

```html
<script type="module">
  import { renderAll } from './dist/index.mjs';
  await renderAll();
</script>
```

The default HTML path downloads `runtime/velocitex-core.wasm` (about 2.1 MB) and
`runtime/katex.min.css` plus the bundled fonts. It does **not** start the .NET
runtime. The stylesheet is injected automatically on first use unless
`init({ injectStyles: false })` is passed; `stylesheetUrl()` returns the injected
URL after initialization.

## Fast HTML path

`render` and `renderToString` use the KaTeX-compatible WebAssembly engine by
default for `html` and `mathml` output. The engine performs parsing, macro
expansion, HTML/MathML construction and serialization in one call and returns the
same markup as `katex.renderToString`. The default returns KaTeX's
`htmlAndMathml` output; `output: 'html'` returns its HTML-only markup and
`output: 'mathml'` returns KaTeX's `<span class="katex"><math …>` wrapper.

Set `fastHtml: false` (or the legacy `fastMathml: false`) or `output: 'svg'` to
force the full-Core SVG contract, which includes a visually hidden semantic
MathML copy and needs the bundled .NET WASM runtime. Invalid formulas still
return `ParseError`; with `throwOnError: false` the engine returns KaTeX's own
readable error markup.

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
- `renderToString(source, options?)`: returns KaTeX-identical HTML by default.
  `output: 'mathml'` selects KaTeX's MathML wrapper; `output: 'svg'` and
  `fastHtml: false` / `fastMathml: false` select the full-Core SVG contract.
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

**Font fallback:** the KaTeX-compatible HTML path uses the unchanged KaTeX fonts
and stylesheet, including KaTeX's CJK/Arabic/etc. fallback classes. `\text{}`
with ordinary CJK text therefore matches KaTeX in the browser. The legacy
full-Core SVG path (`fastHtml: false`) has no DirectWrite shaper in a browser;
missing glyphs there return `UnknownSymbol` or readable original source when
`throwOnError: false`. Full Windows fallback compatibility is not claimed.

Supported formulas use the standalone KaTeX-compatible WebAssembly HTML path.
Other inputs load the bundled .NET WASM AOT runtime. End users need neither the
SDK nor Node.js in the browser. Deploy all runtime assets together when
upgrading the package.

Code is MIT. The HTML engine is the VelociTex C++ core, whose KaTeX-compatible
renderer is a port of KaTeX 0.19.0 under MIT; see `licenses/KaTeX-LICENSE.txt`.
Bundled unchanged KaTeX fonts and their extracted
contours retain OFL-1.1 and Reserved Font Name terms; see
`licenses/KaTeX-FONTS-OFL.txt`. .NET runtime third-party notices are retained with
the distribution.
