# KaTeX-compatible HTML/CSS WebAssembly engine

This directory contains the standalone WebAssembly renderer used by the npm
package for default HTML output. It emits the same HTML markup as KaTeX 0.19.0
(`katex.renderToString` with `output: "htmlAndMathml"`), so the released KaTeX
CSS and fonts typeset it without any VelociTex-specific styles.

The engine is a vendored, patched copy of
[`katex-rs`](https://github.com/katex-rs/katex-rs) (MIT). The patches align the
Rust port with the locked KaTeX 0.19.0 source:

- insertion-ordered attribute maps instead of hash-map iteration order;
- KaTeX's `class =` MathML serialization quirk;
- explicit/implicit inter-atom glue ordering, `\quad` spacing and line breaks;
- small delimiter class duplication and stacked-delimiter font metrics;
- tall radical viewBox padding, minimum-rule thickening, `-0` path formatting,
  `longequal`, line-segment and mapsto stroke geometry, and `\phase` rounding;
- `\reflectbox` / `\mathreflectbox`, `\mapsfrom`, `↤`, `\oiint`/`\oiiint`,
  `\overlinesegment` / `\underlinesegment`;
- `\vcenter`, `\middle`, extensible arrow and `\sqrt` parity fixes;
- UTF-16 diagnostic ranges for non-ASCII input.

`src/lib.rs` exposes a small C ABI (`vt_alloc`, `vt_render`, `vt_result_len`,
`vt_free_result`, `vt_free`) with no JavaScript imports. The browser adapter
loads it with `WebAssembly.instantiate`.

## Build

```powershell
./native/build-katex-html.ps1
```

The script builds `prebuilt/vt-katex-html.wasm` from the vendored source when a
Rust toolchain with the `wasm32-unknown-unknown` target is available. The
committed prebuilt module is the fallback for environments without Rust.

The vendored source is MIT licensed; see `vendor/katex-rs/LICENSE` and the
repository `THIRD-PARTY-NOTICES.md`. The KaTeX markup and CSS remain covered by
the KaTeX MIT license; the bundled fonts retain OFL-1.1/RFN.
