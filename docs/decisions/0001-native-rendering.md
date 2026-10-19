# ADR 0001: DirectWrite/Direct2D through WinUI Composition

Accepted for P1 on 2026-10-19, based on the executable P0 probes. The formula
engine is not implemented by this decision.

Use system DirectWrite and Direct2D through a small source-generated P/Invoke
and explicit-vtable boundary. Use a WinUI `CompositionDrawingSurface` for
display, WIC for PNG, and private DirectWrite glyph outlines for standalone
SVG. Keep the rendering library independent of WinUI. P0 proves private file
font faces; an embedded-memory font loader is not silently assumed to work.

The selected host is WinUI 2.3.9, CsWinRT 2.3.1, SDK BuildTools
10.0.28000.2705, Windows reference package 10.0.26100.87, and .NET SDK
10.0.402. These match the inspected downstream locks. The minimum platform
is 19041; only the recorded Windows build 26200 was actually run. Lock files
are separate where AOT and JIT change implicit dependencies.

The real AOT consumer ran with private fonts, static outline callbacks,
Composition surface interop, transparent exports, dark/light drawing and
resource recreation. `BeginDraw`'s atlas offset is included in the D2D
transform, pixel density is explicit, and the target owns the factory used
for geometry. `EndDraw` errors must still release native references. Drawing
and font owners remain on their documented thread.

No critical interop failure remained that justified switching to Win2D, so
there is one selected surface route. Win2D was not benchmarked and is not
claimed inferior. WARP is the tested software device route; hardware GPU
drawing, injected device removal, and live monitor DPI transitions are
unverified. Releasing and recreating resources is demonstrated, but does not
simulate a driver reset.

For mixed paragraphs choose `IDWriteInlineObject` metrics and a text-renderer
callback. Actual RichTextBlock `InlineUIContainer` measurement failed: short,
fraction, and matrix formula baselines differed from text by approximately
−11.05, −25.05, and −39.05 DIP. The native fallback reports width, height and
baseline and draws body glyphs and formulas from one text layout; both wide
and narrow callbacks meet the same line baseline within 0.001 DIP. Real WinUI
screenshots confirm the result. A fixed margin cannot represent these
height-dependent baseline differences.

The P0 paragraph host offers complete source copying. It does not implement
ordinary body selection, per-formula keyboard focus, or a complete semantic
automation tree. P3 must preserve existing downstream body selection and add
those host behaviors before enabling migration. This prototype therefore
authorizes no downstream replacement by itself. Core continues to own one
formula, while the downstream host owns paragraphs and wrapping.

Maintenance cost is concentrated in the small unsafe ABI boundary: method
slots, GUIDs, struct layouts, callback lifetime, ownership and HRESULT paths
must be reviewed against the exact Windows SDK headers. Keep the real JIT/AOT
probes as regression entry points. Do not add runtime-generated COM stubs or
hide trimming/AOT warnings. A future replacement binding needs equivalent
callback, export, resource-lifecycle and screenshot evidence first.
