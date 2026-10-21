# Third-party notices

VelociTex source code uses the MIT license in `LICENSE`.

KaTeX 0.19.0 code, extracted mathematical rules, the bundled `katex.min.css`
stylesheet and generated metrics originate from KaTeX, copyright Khan Academy
and other contributors. Its MIT license is preserved in
`licenses/KaTeX-LICENSE.txt`.

The browser HTML engine in `native/katex-html` is a vendored, patched copy of
`katex-rs` (https://github.com/katex-rs/katex-rs), copyright the katex-rs
contributors, used under the MIT license. The upstream MIT text is preserved in
`licenses/katex-rs-LICENSE.txt` (also vendored beside the source); the local parity patches are listed
in `native/katex-html/README.md`.

The 20 unchanged KaTeX TrueType fonts bundled in `VelociTex.Core`, and font
outlines used by SVG and browser outputs, retain the SIL Open Font License 1.1.
They are not covered by VelociTex's MIT license. Copyright and Reserved Font
Name declarations are in `licenses/KaTeX-FONTS-OFL.txt`. VelociTex neither
modifies these fonts nor installs them system-wide.

Windows App SDK and other Microsoft runtime components retain their own license
notices when distributed with applications. The browser package carries .NET
runtime license and third-party notice files alongside its runtime assets.

The native formula runtime does not require Node.js, Python, a browser,
JavaScript engine, WebView or Typst. The optional browser package runs in the
browser and supplies the WebAssembly runtime assets it needs.
