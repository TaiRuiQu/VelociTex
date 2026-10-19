# Package and lifecycle contract

The subsequently requested browser/npm adapter is separate from these native
NuGet contracts. Its asynchronous JS API, static-asset setup and known font
limitations are specified in [the npm guide](../npm/velocitex/README.md). It
reuses Core through .NET WASM AOT and the portable SVG renderer; native packages
do not depend on it.

The historical local `0.2.0-preview.1` packages are the P2 integration surface.
`0.4.0-preview.1` is the next local compatibility preview. A version override can
produce a local release candidate after its source and verification gates are
complete. Neither a version string nor packing marks full compatibility,
old-hardware acceptance or a production release complete. The repository
identity in metadata is the user-confirmed GitHub repository; no packing command
pushes source or publishes packages.

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
unbounded attempt. Browser HTML/CSS output modes map to the confirmed native
display and homologous export APIs; there is no runtime DOM renderer.

`pack-local.ps1 -Locked` builds the three packages and portable symbol packages,
checks their payloads and records exact hashes. A clean consumer must separately
restore from the local feed, publish JIT and Native AOT, and run native font,
display and export checks. Versioned consumer lock templates preserve the
historical P2 locks and allow each new candidate to be restored independently.
The pinned SDK supplies SourceLink and GitHub source-control tasks. Verification
must inspect the actual PDB source map and record its commit. A syntactically
valid source URL to an unpublished local revision is not remotely accessible
source evidence. Package structure alone does not prove SourceLink availability,
reproducible binaries or functional clean consumption.
