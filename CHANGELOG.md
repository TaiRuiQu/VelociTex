# Changelog

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
