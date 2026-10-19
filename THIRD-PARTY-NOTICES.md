# Third-party notices

VelociTex source code uses the MIT license in `LICENSE`.

The fixed KaTeX 0.19.0 source, extracted rules and generated metrics originate
from KaTeX, copyright Khan Academy and other contributors. Its MIT license is
preserved separately in `licenses/KaTeX-LICENSE.txt`. Source identity and input
hashes are recorded in `docs/reference/katex-0.19.0.lock.json` and
`docs/reference/katex-0.19.0.files.json`.

The 20 unchanged KaTeX TrueType fonts in
`src/VelociTex.Core/Resources/Fonts` use the SIL Open Font License 1.1. They
are not covered by VelociTex's MIT license. The original copyright and Reserved
Font Names declarations plus full OFL text are in
`licenses/KaTeX-FONTS-OFL.txt`. The project neither modifies nor installs these
fonts system-wide.

Microsoft Windows App SDK, Windows SDK build tools, and CsWinRT are development
and Windows host dependencies, pinned centrally in `Directory.Packages.props`.
Their NuGet packages retain their upstream license notices when copied into
self-contained probe applications. The P0 probe is a development artifact;
there is no NuGet library release yet.

Node.js, TypeScript, Playwright, and pngjs are reference-tool dependencies only.
Python is a font verification tool only. No browser, JavaScript engine, Python,
Typst, or Node.js is a shipped mathematical runtime dependency.
