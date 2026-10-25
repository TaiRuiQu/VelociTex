# Package and lifecycle contract

The browser/npm adapter is separate from these native
NuGet contracts. Its asynchronous JS API, static-asset setup and known font
limitations are specified in [the npm guide](../npm/velocitex/README.md). Its
default path is the C++ core itself, compiled to WebAssembly: it emits
byte-identical KaTeX 0.19.0 HTML and MathML and uses the locked KaTeX stylesheet
and fonts, including KaTeX's CJK/script fallback classes. The browser package
contains no separate JavaScript or Rust formula parser. It only
loads Core through .NET WASM AOT for the explicit SVG/legacy contract
(`fastHtml:false`, `fastMathml:false` or `output:'svg'`); native packages do not
depend on it.

`VelociTex.Core` is .NET 10 and embeds the 20 unchanged, private KaTeX fonts.
`VelociTex.Rendering.Direct2D` adds Windows-only native preparation, drawing, PNG
and SVG. `VelociTex.WinUI` adds `MathView` for a Windows x64 WinUI host. Use normal
Windows App SDK application configuration; the library does not bootstrap an
application. NuGet dependencies are pinned centrally in the repository and
consumers should retain their own lock files.

A self-contained WinUI application can carry WebView2 loader/projection files
through the pinned Windows App SDK distribution. MathView renders through native
Composition and Direct2D; it does not create a browser control or run JavaScript.
This incidental host payload is reported separately from the headless formula
runtime's dependencies.

Compile a complete formula before assigning `MathView.Result`. The immutable
document remains valid after reuse or disposal of its compilation workspace.
One workspace and one explicit macro session may be used by one compilation at
a time. Default formula groups restore local definitions; successful global
definitions survive in an explicit session. `GlobalGroup` can retain top-level
definitions. Errors and cancellation roll back session changes. Independent
workspaces may run concurrently. Renderer and prepared
resources must be accessed and disposed on the creating thread; WinUI must be
accessed on its owning UI thread. Null results clear MathView. The host retains
and shows original input when compilation returns a diagnostic.

`MathEngine.TryCompileToMathMl` is the MathML-only path: it validates, expands
macros and parses the same source but deliberately skips layout, shaping and
drawing-result allocation. Its output is byte-identical to
`MathMlWriter.Write(document)` for the same source and options, and it returns
independently owned warnings. Use `TryCompile` when a drawable document is also
needed.

The optional `VelociTex.NativeMath` adapter adds a C++ fast path:
`NativeMathMl.TryRender` returns presentation MathML for the bounded supported
subset and `false` otherwise; `FastMathEngine.TryCompileToMathMl` retries with
`MathEngine.TryCompileToMathMl` when native parsing is unsupported or options
contain macros, a session, a text shaper or a non-default font size. Native
output has no source annotation and can differ in structure and whitespace from
the managed writer; callers that need the exact managed bytes must use
`MathEngine.TryCompileToMathMl` directly. The native adapter finds
`VelociTex.NativeMath.dll` beside the app or via `VELOCITEX_NATIVE_DIR`.

The npm `RenderOptions.fastHtml` (legacy alias `fastMathml`) defaults to true.
Fast `html` output is the byte-identical KaTeX HTML/CSS contract; `output:'svg'`
or `fastHtml:false` forces the exact C# SVG contract. The bundled KaTeX CSS is
injected on first use and can be linked manually instead.

`MathView` reuses its renderer, WARP device and graphics device across result and
DPI changes. Views on the same UI thread share those device resources, and the
last unloaded view releases them; no device is created per formula. Per-view
Composition surfaces and prepared results remain independently owned.

MathView has a focus stop and a scoped Control+C accelerator. Copy uses the
entire `MathDocument.Source`, including source that scrolls outside the viewport.
`AccessibleDescription` supports binding and supplies a human-readable UIA
name; hosts may instead set `AutomationProperties.Name`. The read-only UIA
Value always exposes the original TeX. This basic accessible representation is
not semantic spoken-math navigation. A full screen-reader audio acceptance run
is a separate check from the live peer/provider test.

Dimensions are DIP. Glyph coordinates use logical-box coordinates, and ink may
overhang the logical box. The control and exports include ink bounds. No host
paragraph wrapping is performed. Export streams remain caller-owned and open.
Ordered `PaintCommands` reference immutable glyph, rule and path arrays and
deduplicated color/affine states. Hosts must preserve their order. An empty
ordered command list can intentionally represent a completely invisible formula.
Actual fallback text provenance is retained separately from the outline paths.
`IMathTextRunShaper` supports actual contextual runs; the Windows implementation
uses DirectWrite. Font availability can differ across Windows installations.

Input length, node, nesting, token and macro budgets are in `MathOptions`;
unsupported syntax and resource exhaustion return explicit codes. Cancellation
is cooperative. Native surfaces and exports additionally reject dimensions over
8,192 pixels or 4,194,304 pixels total, rather than allocating unbounded images.

Treat formula input as untrusted data. No external resources, JavaScript,
browser, Typst, package restore, caller-defined TeX callback code or script are evaluated during
compilation. Built-in core callbacks are implemented as bounded C# semantics.
Optional .NET strict handlers and shaping providers are trusted host callbacks;
their exceptions propagate after workspace cleanup. `MathDocument.Warnings`
owns bounded strict-mode warnings and detaches caller callback references.
Resource limits do not replace application-level concurrency and
request limits. Retain diagnostic ranges as UTF-16 source offsets; do not expose
filesystem paths or stack traces to an untrusted client.

The package SPDX expression is `MIT AND OFL-1.1`, reflecting separate payloads.
`LICENSE` covers VelociTex code, `licenses/KaTeX-LICENSE.txt` covers the upstream
code/rules, and `licenses/KaTeX-FONTS-OFL.txt` contains font copyright, Reserved
Font Names and the full OFL. Font names and bytes are unchanged. The runtime does
not install fonts system-wide. Development scripts, browsers, Node packages and
reference data generators are excluded from the nupkg.

`TryCompile` keeps its stable structured false/diagnostic failure contract.
`CompileForPresentation` separately returns literal original-source drawing data
for ordinary syntax errors when enabled, in the chosen explicit error color.
Cancellation, busy workspaces and resource exhaustion do not trigger a second
unbounded attempt. Native drawing and exports use the same retained formula data.
