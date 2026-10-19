# P1 API and ownership

P1 implements a bounded basic subset of the locked KaTeX 0.19.0 rules. It is a
source/project API, with no published package or stability promise yet. See the
[phase record](verification/p1-2026-10-19.md) for tested scope and limitations.

```csharp
using VelociTex;
using VelociTex.Rendering.Direct2D;

var engine = new MathEngine();
using var workspace = new MathWorkspace();
var options = new MathOptions { FontSize = 24, DisplayMode = true };
if (engine.TryCompile(@"\frac{a}{b}+\sqrt{x}", in options, workspace,
    CancellationToken.None, out var document, out var diagnostic))
{
    // Windows only. Initialize once and dispose on the same thread.
    using var renderer = new WindowsMathRenderer();
    using var png = File.Create("formula.png");
    renderer.WritePng(document!, png); // Leaves the caller's stream open.
    using var svg = File.Create("formula.svg");
    renderer.WriteSvg(document!, svg);
    string mathml = MathMlWriter.Write(document!);
    // A WinUI MathView accepts Result = document. It uses native commands,
    // never PNG or SVG as the display intermediate.
}
else
{
    // Keep the original input in the host; use Code and Range (UTF-16).
    Console.WriteLine($"{diagnostic.Code}: {diagnostic.Range}");
}
```

`MathEngine` has immutable shared rules and permits concurrent calls with
different workspaces. The explicit workspace reuses parser/macro scratch and
rejects concurrent use. Its `Dispose` releases retained capacity and allows
subsequent reuse; it owns no native resources. The convenience `TryCompile`
creates a workspace per call. Layout still allocates bounded temporary arrays
per call; there is no formula result cache.

`MathDocument` retains the original source, options without external macro or
session references, compact semantic nodes, and independent result arrays.
Glyphs, rules and normalized paths are exposed read-only. A document survives
workspace reuse, errors, cancellation and disposal. Coordinates are DIP, x grows
right and y down; the logical box starts at zero, and `Baseline` is measured
from its top. Ink may overhang this box. Exports union the logical and ink boxes
before adding padding; neither the formula nor its font is automatically shrunk.

`MathOptions.Macros` accepts string replacements and `#1` through `#9`.
`\def`, `\gdef`, and basic `\newcommand`/`renewcommand`/`providecommand`
support undelimited parameters. Each default compile is isolated. An explicit
`MacroSession` is serial, commits only after successful layout, and rolls back
on an error or cancellation. A session obeys aggregate source/token budgets;
it does not permit callback code, external resources or scripts.

Defaults are 32,768 input UTF-16 units, 65,536 semantic nodes, 65,536 tokens,
128 nesting levels and 1,000 macro expansions. Macro definitions and arguments
also consume bounded budgets. Ordinary invalid/unsupported input returns
`false`, a null document and a structured diagnostic. Null API arguments throw.
Cancellation returns `Cancelled` and leaves workspace/session reusable.

`WindowsMathRenderer` owns private font faces, Direct2D resources and an outline
cache bounded by 4,096 entries and a 16 MiB string-size estimate. The default
constructor extracts the unchanged embedded fonts into an owned temporary
directory; disposal releases faces first, then deletes that directory. An
explicit font directory may be supplied and remains caller-owned. Renderer and
`PreparedMath` operations are confined to their creating thread. A preparation
belongs to one renderer and becomes unusable when disposed or its owner is
disposed. `Draw` takes an `ID2D1RenderTarget`/device-context pointer; the caller
owns `BeginDraw`/`EndDraw`. It restores transform and text antialias state.

PNG/SVG accept file paths or any writable `Stream`; they do not close caller
streams. PNG is transparent WIC output, bounded to 4,194,304 pixels and an 8,192
pixel side. PNG encoding uses a native in-memory stream plus a 64 KiB transfer
buffer. SVG contains private glyph outlines with local `defs`/`use`, rule
rectangles and normalized radical paths, with no external fonts or resources.
MathML is generated from semantics, including the original TeX annotation by
default. Serialization and export buffers allocate and are measured separately.

`MathView` currently binds a precompiled `Result`, supports natural dimensions,
foreground, Composition density, unloading/reloading and an accessibility name.
The sample compiles source and displays original text on error. Source binding,
the full error strategy, keyboard copying, screen-reader acceptance and package
API stabilization remain P2 work.
