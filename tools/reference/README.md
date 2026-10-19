# Fixed KaTeX reference tooling

These tools are **development only**. Nothing in this directory is a shipped
VelociTex runtime dependency. The original upstream lock is never rewritten.

Run from this directory with Node **22.23.2**, npm **10.9.8**:

```powershell
npm ci --ignore-scripts --no-audit --no-fund
npm run acquire
npm run inventory
npm run check-source-dist
npm run check-normal-corpus
npm run reference -- --full-matrix
npm run inspect-fonts
npm run check-baseline
npm run check-evidence
```

`npm run verify` runs the same sequence after dependencies are installed.
`npm run font-calibration` separately consumes the P0 font generator's
`artifacts/fonts/font-calibration.html` and writes its JSON and screenshot.
`npm run verify-svg` separately opens the native JIT/AOT probe's four SVGs,
asserts they have paths and no text/font/external resource dependencies, and
writes standalone browser previews. Native probes must have run first.

The browser executable defaults to the exact locally installed Chrome recorded
in `docs/reference/development-toolchain.lock.json`. Set `VELOCITEX_BROWSER` to
another path containing the **same locked bytes** when needed. Version or hash
drift fails. Browser acquisition is not fully portable: the official pinned
Playwright Chromium download did not progress on this machine. Installing an
arbitrary newer browser is not a substitute for this lock.

`acquire.mjs` checks the tag's commit, npm SHA-512 and SHA-256, all 20 original
font checksums, and every source archive file against the commit's Git blob
ID. It writes source/CSS/TTF/WOFF2/metrics checksums to a separate manifest.
`inventory.mjs` instruments TypeScript **AST calls**, then executes the exact
transitive registration modules. Actual final function, environment, macro,
symbol and settings registries determine the denominator; loops and generated
Unicode aliases are executed normally. Exclusions are explicit named decisions,
never file-name guesses. Reserved fixture IDs have `fixtureStatus: planned`:
they do not imply that the C# implementation or its coverage exists.

`reference.mjs` uses a fixed xorshift seed, fresh macro objects, exact CSS/fonts,
font readiness checks, real browser DOM and per-code-point ranges, baseline
markers, MathML namespace/tree normalization, and screenshot ink bounds. It
asserts that KaTeX's outer 1.21 scaling produces the requested effective em.
MathML text nodes, including whitespace inside `mtext`, remain intact. Each
scenario records the measured font readiness and visual glyph CSS styles.
`inspect-fonts.mjs` adds actual platform font selection through Chromium's CSS
debugging protocol, including system fallbacks.

Five repeated captures per valid seed measure tool noise. The timing samples
measure warmed rendering and forced synchronous DOM layout only; they exclude
actual presentation, frame waits and screenshot encoding. No native speedup,
full-core compatibility, or old-hardware acceptance follows from these results.

Large upstream archives, browsers and reference images live under ignored
`artifacts/`. Tracked manifests, code, dependency lock and verification notes
retain the provenance and reproduction entry points.
