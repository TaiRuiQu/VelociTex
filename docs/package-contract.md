# Preview package contract

The local `0.2.0-preview.1` packages are a P2 integration surface. The version does
not assert full KaTeX compatibility, downstream acceptance, old-hardware timing
acceptance or completion of the later release gates. There is no external feed
publication or repository URL in package metadata.

`VelociTex.Core` is .NET 10 and embeds the 20 unchanged, private KaTeX fonts.
`VelociTex.Rendering.Direct2D` adds Windows-only native preparation, drawing, PNG
and SVG. `VelociTex.WinUI` adds `MathView` for a Windows x64 WinUI host. Use normal
Windows App SDK application configuration; the library does not bootstrap an
application. NuGet dependencies are pinned centrally in the repository and
consumers should retain their own lock files.

Compile a complete formula before assigning `MathView.Result`. The immutable
document remains valid after reuse or disposal of its compilation workspace.
One workspace and one explicit macro session may be used by one compilation at
a time. Independent workspaces may run concurrently. Renderer and prepared
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
Input length, node, nesting, token and macro budgets are in `MathOptions`;
unsupported syntax and resource exhaustion return explicit codes. Cancellation
is cooperative. Native surfaces and exports additionally reject dimensions over
8,192 pixels or 4,194,304 pixels total, rather than allocating unbounded images.

Treat formula input as untrusted data. No external resources, JavaScript,
browser, Typst, package restore, callback macros or script are evaluated during
compilation. Resource limits do not replace application-level concurrency and
request limits. Retain diagnostic ranges as UTF-16 source offsets; do not expose
filesystem paths or stack traces to an untrusted client.

The package SPDX expression is `MIT AND OFL-1.1`, reflecting separate payloads.
`LICENSE` covers VelociTex code, `licenses/KaTeX-LICENSE.txt` covers the upstream
code/rules, and `licenses/KaTeX-FONTS-OFL.txt` contains font copyright, Reserved
Font Names and the full OFL. Font names and bytes are unchanged. The runtime does
not install fonts system-wide. Development scripts, browsers, Node packages and
reference data generators are excluded from the nupkg.

`pack-local.ps1 -Locked` builds the three packages and portable symbol packages,
checks their payloads and records exact hashes. A clean consumer must separately
restore from the local feed, publish JIT and Native AOT, and run native font,
display and export checks. SourceLink needs a confirmed repository identity;
no fictional URL is emitted. Package structure verification alone does not prove
SourceLink, reproducible binary builds or functional clean consumption.
