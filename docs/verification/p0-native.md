# P0 native drawing and export evidence

Recorded 2026-10-19. This is a **hard-coded feasibility probe**, not a TeX parser, layout engine, or compatibility fixture. It contributes zero formula compatibility coverage.

## Implemented route

`src/VelociTex.Rendering.Direct2D` uses source-generated `LibraryImport` entry points and explicit Windows SDK COM vtables, without a binding NuGet package. The ABI is checked against the installed Windows SDK 10.0.26100.0 headers (`d2d1.h`, `d2d1_1.h`, `dwrite.h`, `wincodec.h`). The target is .NET 10 / Windows 10.0.26100.0, minimum host platform 19041; Windows SDK reference package 10.0.26100.87. No runtime-generated COM stubs, browser, JavaScript, Node.js, or external font installation is required.

The drawing opens private `KaTeX_Main-Regular.ttf` and `KaTeX_Math-Italic.ttf` faces with `IDWriteFactory.CreateFontFileReference/CreateFontFace`. A separate native-font pass opens **all 20 locked TTF faces**, obtains their native glyph counts, and checks every one of the 2,195 generated cmap code points against `IDWriteFontFace.GetGlyphIndices`. Both JIT and AOT observe 2,255 total glyphs, zero missing expected code points, and zero glyph ID mismatches. `report.json/privateFonts` lists the native count, checked code-point count, missing code points, and mismatches for every face. Absent characters outside a face's expected cmap are not counted as failures. The development probe links the generated `FontData.g.cs` only for this independent native comparison. The shipped locked font bytes remain unchanged. Font metrics, licensing, and checksums are covered by the separate font verification record. This probe deliberately uses TTF advances for explicitly positioned demonstrations; it does not substitute those advances for KaTeX's TeX layout metrics.

The shared hard-coded drawing contains actual `DWRITE_GLYPH_RUN` submissions, two filled rule rectangles, and a filled radical path. The PNG route creates a transparent WIC PBGRA bitmap and native PNG encoder. The WARP route creates a BGRA-capable Direct3D11 WARP device, a Direct2D device/context and target bitmap, then copies through a CPU-readable staging bitmap to WIC. The SVG route calls `IDWriteFontFace.GetGlyphRunOutline` into an unmanaged static-vtable geometry sink, emits reusable `defs/path/use`, and exports the same rules and radical coordinates. All `href` values are internal; no installed fonts, scripts, or external images are referenced.

`NativeProbeRenderer.Draw(nint, ProbeTheme, float, float)` accepts an `ID2D1RenderTarget` or its `ID2D1DeviceContext` subclass. The caller owns BeginDraw/EndDraw and the target. Width/height/baseline are 640/180/112 DIP. The renderer restores transform and text antialias state after drawing. Geometry is created through the target's factory to avoid `D2DERR_WRONG_FACTORY`; this was exercised and corrected during the initial WARP run. `CreateWarpD2DDevice()` returns an owned COM reference for the Composition host. A probe instance is confined to one thread and disposed there.

## Executed checks

- Release JIT build and real new-process execution: passed, zero warnings.
- Release Native AOT publish and actual native EXE execution: passed, zero warnings; `RuntimeFeature.IsDynamicCodeSupported == false`.
- Transparent PNG: 100/125/150/200% output scales × light/dark × WIC/WARP = 16 files for each runtime.
- Private font glyph lookup: every selected glyph is nonzero; missing glyphs throw.
- JIT/AOT PNG bytes match at every tested scale, theme and backend; WIC/WARP PNG bytes also match.
- All PNGs have the expected pixel dimensions, both transparent and opaque pixels, grayscale edge coverage, and ink strictly inside the canvas.
- Standalone SVGs were opened by an independent Chrome 155.0.8059.84 process through development-only Playwright; four scales and both themes were captured. SVG/native alpha ink bounds differ by at most 1 pixel. Whole-canvas alpha RMSE is recorded, rather than mistaken for exact raster identity.
- The native/SVG light/dark contact sheet was opened and visually inspected: glyph order, superscripts, fraction rule, radical, matrix rows, and transparent padding are present without clipping.
- Five complete renderer unload/reload cycles, with a new WARP device/context/bitmap each time, produce the same PNG hash. Each ordinary WARP export also recreates all device resources.

Raw files are under `artifacts/native/jit/`, `artifacts/native/aot/`, `artifacts/native/image-audit.json`, and `artifacts/native/visual-comparison.png`. Large artifacts are regenerable and follow the repository ignore policy. The executable is in `artifacts/native/publish/`.

The final recorded AOT batch took about 272 ms for all 20 font checks, 18 exports, reload and paragraph probes and allocated about 11.53 MB of managed memory including export buffers and verification reads. Process private bytes at the recorded checkpoint were about 16.76 MB. These are whole-probe snapshots, **not** per-formula timings, native allocation totals, steady-state cache budgets, GPU residency, or evidence for the engine's ≤10 ms / later ≥10× goals. Subsequent reruns legitimately change these snapshots. The native EXE is approximately 2.4 MB before its separate fonts/PDB; this is not a final NuGet size budget.

## DirectWrite paragraph baseline fallback

`NativeInlineBaselineProbe.WriteReport(path)` creates real `IDWriteTextLayout` instances with private static-vtable `IDWriteInlineObject` and `IDWriteTextRenderer` callbacks. A hard-coded fraction reports 110 × 60 DIP with baseline 42; a matrix reports 110 × 74 DIP with baseline 43. The content includes body text and punctuation around both objects.

At width 780 DIP, DirectWrite produces one 74-DIP line with baseline 43. Fraction top is 1 and matrix top is 0. At width 280 DIP it produces two lines: height/baseline 60/42 and 74/43, placing the matrix at top 60 and absolute baseline 103. Every callback's body glyph baseline and every inline top + declared baseline matches an actual line baseline within 0.001 DIP. Both inline objects return to their original reference count after text-layout release. This passed under JIT and Native AOT; raw results are `inline-baselines.json` in both output directories.

`DrawParagraph(nint renderTarget, float width, ProbeTheme theme, string? fontDirectory)` also draws those body glyphs, fraction and matrix through the callbacks and returns `InlineParagraphMetrics` with measured height, first baseline, and line count. Its default private-font location is `AppContext.BaseDirectory/Fonts`. The real WinUI host/screenshots and the RichTextBlock route decision are documented separately by the host probe. These metrics prove the DirectWrite callback route, not a finished paragraph API, selection model, clipboard mapping, or UIA implementation.

`MeasureParagraph(float width)` uses the same layout to let the host size its surface to the actual line height. These probe methods intentionally retain full report/validation allocations, including a memory stream when no report path is requested; they are not a benchmarkable engine hot path.

## Reproduction

```powershell
dotnet build tools/VelociTex.Native.Probe/VelociTex.Native.Probe.csproj -c Release -p:PublishAot=false -t:Rebuild
dotnet tools/VelociTex.Native.Probe/bin/Release/net10.0-windows10.0.26100.0/win-x64/VelociTex.Native.Probe.dll artifacts/native/jit
dotnet publish tools/VelociTex.Native.Probe/VelociTex.Native.Probe.csproj -c Release -r win-x64 -p:PublishAot=true -o artifacts/native/publish
artifacts/native/publish/VelociTex.Native.Probe.exe artifacts/native/aot
python tools/VelociTex.Native.Probe/verify-native.py artifacts/native
```

The development image audit pins Pillow 11.1.0 and Python Playwright 1.55.0 in `tools/VelociTex.Native.Probe/requirements.txt`; it also checks the browser SHA-256 against the reference toolchain lock. It is an independent inspection tool, separate from the KaTeX reference generator. If the same locked Chrome binary is stored elsewhere, pass `--browser <absolute-executable-path>`. The initial restricted task-host launch failed; repeating the AOT publish with the ordinary approved .NET toolchain subprocess permission succeeded. No AOT warning suppression was added.

The probe uses `packages.jit.lock.json` when `PublishAot=false`, and `packages.lock.json` for AOT; the rendering library uses `packages.lock.json` for JIT and `packages.aot.lock.json` for the propagated AOT configuration. Both complete project graphs passed `dotnet restore --locked-mode` with their respective properties. Separate locks prevent ILCompiler/runtime-identifier changes from invalidating the ordinary JIT solution restore. Restricted NuGet audit access initially produced NU1900; the approved locked restore with metadata access succeeded without suppressing audit.

When running the JIT DLL again after AOT publishing, first rebuild with `PublishAot=false`: the AOT build's intermediate runtime configuration disables dynamic-code support even when that managed DLL is launched with `dotnet`. The audit asserts the actual JIT report has dynamic code enabled and the standalone native executable report has it disabled; it does not identify the two modes solely by an output-directory name.

## Explicitly unverified here

Actual GPU device removal/reset injection, hardware D3D rendering, changing the live Windows monitor DPI between displays, native allocation tracking, long-duration resource-pressure acceptance, accessibility and copy semantics, old-CPU performance, and real mathematical layout are unverified by this console probe. Deliberate release/recreation verifies owned-resource lifecycle but is not a simulated `DXGI_ERROR_DEVICE_REMOVED`. Real WinUI display and AOT screenshot acceptance belong to the host evidence; exporting four DPI sizes alone does not establish live UI DPI behavior.
