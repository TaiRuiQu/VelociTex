# Changelog

## C++ fast pre-render — 2026-10-20

- Add `native/VelociTex.NativeMath.cpp`: an allocation-light C++ parser and
  presentation-MathML writer with no AST and bounded input/depth/node counts.
- Add `VelociTex.NativeMath` P/Invoke adapter and `FastMathEngine`, which tries
  the native path and falls back to the full managed engine for unsupported
  syntax or non-default options.
- Add an Emscripten `-O3` build of the same parser to a 24 KB WASM module; the
  npm binding now uses it by default for supported formulas, before starting the
  .NET runtime. `fastMathml:false` preserves the exact C# SVG/MathML contract.
- Achieved: native MathML **14.2x–17.3x** vs KaTeX MathML, WASM MathML parse
  **3.9x**, single DOM **3.47x**, 100-formula page **4.76x sync / 4.63x paint**.
  See `docs/verification/performance-cpp-2026-10-20.md`.

## Performance work — 2026-10-20

- Add cached built-in macro lookup, fast ordinary-symbol parser/macro paths,
  static span-keyed symbol/command/glyph maps, pooled MathML nodes and a
  semantics-only MathML API.
- Add cached portable-SVG path geometry, cached generated stretchy/radical path
  arrays and a fast fixed-point coordinate formatter.
- Reuse native renderer/WARP/Composition resources per `MathView` and share them
  across the controls of a page; cache private-font validation and Direct2D
  glyph geometries.
- Add WASM output selection, direct SVG/MathML string exports, default-limit fast
  exports and AOT diagnostics; publish the AOT runtime into the npm dist.
- Add reproducible native/KaTeX/WASM/WinUI benchmarks for single formulas,
  complex typesetting and a 100-formula page. This C#-only baseline measured
  native SVG 3.54x and WinUI page 2.49x; its MathML/WASM rows are superseded by
  the C++ record above. See `docs/verification/performance-2026-10-20.md`.

## Optional web preview — 2026-10-19

- Extend `v*` release CI to publish NuGet and npm together at the tag version,
  using npm Trusted Publishing after actual archive/browser verification.

- Add a separate .NET WASM AOT binding, portable SVG backend, npm ESM package,
  TypeScript types and asynchronous `render`, `renderToString` and `renderAll`.
- Add deterministic locked-font contours, static asset deployment helper,
  isolated npm/browser consumers and a dedicated web CI workflow.
- Publish `velocitex@0.1.0-preview.2` to npm's `next` channel with four rendering
  comparison images in the README and tarball. Benchmark code and results are
  excluded from this distribution.
- Browser fallback fonts remain incomplete; 29 otherwise-valid P4 inputs
  explicitly report missing glyphs. This npm preview does not replace the frozen
  native candidate. See
  `docs/verification/wasm-2026-10-19.md`.

## Unreleased release-candidate preparation

- Add ordered native painting, explicit colors and affine transforms; native,
  PNG, SVG and MathML share retained formula semantics.
- Extend the locked KaTeX 0.19.0 parser and layout with core constructions,
  environments, macro callbacks, source-compatible argument boundaries and
  structured diagnostics.
- Retain strict-mode warnings and actual contextual Windows text outlines.
- Add explicit original-source error presentation without changing
  `TryCompile`'s false/diagnostic contract.
- Prepare versioned isolated JIT and Native AOT package consumers, source maps,
  XML API documentation, input boundaries and package-payload checks.

These entries describe implementation work. The phase verification records
determine acceptance; this section does not mark P4, P5, a 1.0 release, old-CPU
performance or external publication complete.

## 0.2.0-preview.1 — 2026-10-19

Local P2 preview with common mathematical formulas, all three exports, native
WinUI display, whole-formula copying, basic UI Automation and isolated JIT/AOT
package consumption. Original font bytes and separate MIT/OFL notices retained.
See `docs/verification/p2-2026-10-19.md` for executed checks and remaining work.

## P1 — 2026-10-19

Compact parsing and layout pipeline, workspace and macro-session lifecycle,
native drawing, PNG/SVG/MathML and actual Native AOT verification. See the P1
phase record for source hashes, raster differences and measured limits.
