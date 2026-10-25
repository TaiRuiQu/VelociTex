# Third-party notices

VelociTex source code uses the MIT license in `LICENSE`.

KaTeX 0.19.0 code, extracted mathematical rules, the bundled `katex.min.css`
stylesheet and generated metrics originate from KaTeX, copyright Khan Academy
and other contributors. Its MIT license is preserved in
`licenses/KaTeX-LICENSE.txt`.

The browser HTML/CSS and MathML engine is the VelociTex C++ core itself: the
KaTeX-compatible renderer in `native/core/html` is a port of KaTeX 0.19.0, so
the KaTeX MIT license above also covers that port. The browser package ships
no separate JavaScript or Rust formula parser.

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
