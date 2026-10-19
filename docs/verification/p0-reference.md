# P0 reference baseline — 2026-10-19

P0-01 and P0-02 are implemented. P0-06 has real reference-side output and noise
evidence. These are development tools; no C# formula compatibility is claimed.
P0 native feasibility, actual presentation timing and old-machine performance
are separate work packages.

## Upstream acquisition and provenance

The tag resolves to locked commit
`6ea2dc97188f0cc282e05a9690418ae7fe5457ba`. The npm package SHA-256 is
`d8e49f2fea6eeed7cdae2cf4fd48e2f1d348758aa9e5740715e4533c2c96d1ee`;
the complete original SHA-512 integrity and all 20 locked TTF hashes match.
The source archive SHA-256 is
`c828240a2abd1f8603297831681b0c05c1d4f709341072b0901acb1d4ab8c5b5`.
All **722** source files were additionally verified against the commit's Git
blob IDs; archive naming alone was not trusted. The npm archive contains 225
files. Exact source, metrics, CSS, TTF and WOFF2 checksums are recorded in
`docs/reference/katex-0.19.0.files.json`. The original lock was left intact.
The original MIT code license is preserved in `licenses/KaTeX-LICENSE.txt`;
font licensing is handled separately by the font work package.

The source and distribution implementations agree on normalized semantics,
exact MathML serialization and errors for all 26 non-excluded seed fixtures.
Raw checks are in `artifacts/reference/source-dist-check.json`.

## Scope inventory

`compatibility/core-0.19.0.json` contains **2,947** classified items:

| Kind | Count |
| --- | ---: |
| Registered functions | 312 |
| Environments | 33 |
| Built-in macros | 339 |
| Symbols, including mode-specific Unicode aliases | 2,238 |
| Options | 14 |
| Implicit parser commands | 4 |
| Independently registered builders | 7 |

The generator instruments registration **AST calls** and executes upstream's
real transitive module graph. Final registry snapshots determine the list.
Every item has a source line, commit, classification, reserved fixture ID and
six implementation/verification states. There are no unclassified entries.
**2,939 are included; 8 are excluded**: the seven named resource/DOM injection
commands and the `trust` setting. The `output` option remains included for
required MathML semantics, with browser-only values individually annotated.
`macros` and `strict` similarly distinguish core values from user JavaScript
callback extension values. `CD`, core built-in callback
macros, `\html@mathml`, all other internal helpers and rare math commands remain
included. The entire inventory is currently unimplemented on the C# side.
`coverage-*` fixture IDs are explicitly planned, rather than fictitious tests.
The 21 valid seeds are not a complete generated command corpus.

The complete 14-option denominator is `displayMode`, `output`, `leqno`, `fleqn`,
`throwOnError`, `errorColor`, `macros`, `minRuleThickness`, `colorIsTextColor`,
`strict`, `trust`, `maxSize`, `maxExpand` and `globalGroup`. Source write-site
review found registry mutations only inside the corresponding `define*`
functions; no direct registration branch was missed by the call instrumentation.

The manual exclusion review retained these exact decisions:

| Item | Decision |
| --- | --- |
| `\href` | External hyperlink behavior excluded |
| `\url` | URL/hyperlink interaction excluded |
| `\includegraphics` | External image and resource loading excluded |
| `\htmlClass` | DOM class injection excluded |
| `\htmlId` | DOM ID injection excluded |
| `\htmlStyle` | Browser CSS injection excluded |
| `\htmlData` | DOM attribute injection excluded |
| `trust` | Browser/resource trust callbacks excluded |

The three mixed-value options are not dropped: `output` retains MathML export
semantics, `macros` retains core definition values and `strict` retains its
boolean/enumerated policy values. User JavaScript callback extension values and
HTML serialization values are explicitly marked outside the native scope.

## Browser evidence

The measured toolchain is Node 22.23.2, npm 10.9.8, Playwright 1.56.1,
TypeScript 5.9.3 and pngjs 7.0.0, locked by npm's dependency lock.
The actual browser is Chrome **155.0.8059.84**, executable SHA-256
`6aa5d599bbc3a0b3842af9f44b18fd29989eb3cc21b4b3ec7489e12f36e9962e`.
The separate toolchain lock fails on version or executable drift. Automatic
acquisition of the fixed Playwright Chromium remains **unverified**: its official
v1194 download showed no progress and was canceled. The local Chrome binary is
not vendored, so another machine needs the same locked browser bytes to
reproduce raster results exactly.

All 29 seeds were classified as expected: **21 valid, 5 parse errors, 3 excluded**.
The 21 valid fixtures were captured at 16/24/32 DIP, 100/125/150/200% device scale,
and both themes: **504 reference screenshots**. Every scenario waits for all
exact distribution font faces to load and asserts the effective KaTeX em, using
`host em = FontSizeDip / 1.21`. Each capture contains DOM node geometry,
per-code-point measured ranges, baseline, raster ink bounds, normalized MathML
and screenshot SHA-256. MathML accessibility markup is excluded from visual
geometry. Text whitespace is preserved in normalization.

Code review corrected the interpretation of the `.katex-html` rectangle: it is
a container-sized box in display mode and a font line box inline, so it cannot
represent natural formula size. `logical` now comes from the union of measured
`.katex-base` rectangles, whose struts encode real height/depth. The original
rectangle remains `htmlBox`; tags are separate `tagBoxes`, with advance, ascent
and depth explicitly recorded. Existing raw node measurements were sufficient:
`derive-geometries.mjs` derived all 504 results without replacing screenshots.
All original geometry files were preserved/reconstructed byte for byte against
their original SHA-256 as `reference.geometry.raw.json`, and the exact original
capture generator is archived with its recorded hash. The derivation manifest
records input/output hashes. `check-evidence` independently re-derives every
logical box from those original measured bases.

The capture manifest retains the tools present at capture time. Subsequent
scope review changed the inventory's `output` classification, and the geometry
derivation changed the generator's interpretation of preserved DOM data; these
did not alter captured pixels. The tracked evidence manifest separately hashes
all current tool sources, the revised inventory and the derivation record.

An independent baseline check used the real DOM's direct `.katex-strut` bottom
plus computed `vertical-align` instead of the appended marker. Across all 21
valid fixtures, adding the zero-size marker changed no formula bounding box
(**0 DIP**), and the marker/strut baselines differed by at most **0.01515 DIP**,
within Chromium's 1/64-DIP layout rounding. This check lives in
`artifacts/reference/baseline-check.json` and can be repeated with
`npm run check-baseline`.

The seed and scenario order uses fixed seed `0x19002026`. Every valid seed was
independently rendered and captured five further times: **105 repeat captures**.
The maximum geometric spread was **0 DIP**, and all five screenshot hashes
matched for every fixture. These samples support keeping the existing P0
geometric tolerances; they do not calibrate native raster differences yet.

Chromium platform font inspection records the real family/PostScript selection
in `artifacts/reference/platform-fonts.json`; Chinese text used **Noto Sans SC**
alongside the custom KaTeX faces. This fallback depends on the recorded local
Windows font environment. No claim of cross-machine fallback equivalence is made.

The associated font calibration page was also run: 20/20 private TTF faces
loaded, all eight formula samples rendered, and CSS changed the 20px host's
KaTeX font to 24.2px. The x advance changed 10.5625→12.78125px while the measured
baseline stayed equal. `artifacts/fonts/font-calibration.png` was visually
reviewed: the 20 font samples, baseline/axis guides and eight italic/fraction
samples are visible. Its generator and detailed font interpretation belong to
the font work package.

Raw artifacts are under `artifacts/reference/<fixture>/<scenario>/` plus
`reference-noise.json`, `run-manifest.json`, `summary.json` and
`browser-timing.samples.json`. The reference-side timing includes **1,050**
warmed render-plus-forced-layout samples after 100 warm-ups. It excludes actual
drawing/presentation, frame waits and screenshot encoding, so it is **not** a
complete visual-work benchmark or an algorithmic speedup denominator.

## Reproduction and remaining work

```powershell
Set-Location tools/reference
npm ci --ignore-scripts --no-audit --no-fund
npm run verify
npm run font-calibration
```

`check-evidence.mjs` checks all screenshot hashes, nonempty ink/glyph output,
cross-scenario normalized MathML invariance and repeated-render noise, then
writes a compact tracked digest manifest at
`docs/reference/p0-reference.evidence.json`. Running the inventory again produces
the same bytes. Screenshots are reference evidence, not host WinUI acceptance.

Not executed here: C# semantic/geometric/MathML differential tests, native PNG/SVG
comparisons, complete per-command fixture generation, old-CPU measurements,
actual browser presentation tracing, and portable pinned-browser acquisition.
The next reference task is to turn planned coverage IDs into runnable fixtures
as each core feature is implemented, preserving this original denominator.

## Separate SVG opening check

`npm --prefix tools/reference run verify-svg` independently opened all four
native probe SVGs (JIT/AOT × light/dark) in the locked browser. Each contains
17 paths, zero font faces, zero text/image/foreignObject/style/link/script
elements, only local fragment references, and no resource requests beyond the
SVG document. The output therefore draws from embedded private-font outlines
without requiring an installed KaTeX font. Four previews and their source/
screenshot hashes are in `artifacts/reference/native-svg-check.json`.
The AOT light preview was visually reviewed: glyphs, fraction rules, radical
and the two matrix rows are present. This check opens the native feasibility
probe's SVG and is separate from C# formula compatibility or native/browser
pixel-equivalence acceptance.

## Frozen normal performance corpus

`benchmarks/corpus.normal.json` freezes **60** formulas for the P1 and later
normal-workload benchmarks: ten each for short algebra/scripts,
fractions/radicals, calculus, aligned environments, 3×3/5×5 matrices, and short
math text with moderate nesting. Every category has equal weight (1/6), and
all ten inputs within a category have equal weight. The matrix category includes
five 3×3 and five 5×5 examples. Rare-feature compatibility cases such as CD and
invalid/stress inputs remain in their separate corpora.

The file fixes the shuffle seed, 24-DIP rendering configuration, initialized
fonts/devices, complete-call timing boundary and cache policy: formula,
semantic-result and layout-result caches are disabled, and glyph/shaping cache
states must be reported separately. Its 1,024-UTF16 normal-input envelope is a
corpus boundary, rather than a claim about final public engine resource limits.
The longest frozen input currently has 99 UTF16 code units.

`npm --prefix tools/reference run check-normal-corpus` actually parsed all 60
inputs and produced MathML through locked KaTeX 0.19.0, checked declared matrix
row/column counts, and recorded per-fixture semantic/MathML hashes in
`docs/reference/normal-corpus-0.19.0.check.json`. The frozen corpus SHA-256 is
`0c9f4daf48a372eba2b297e346bc5a7dd11ac54e816a2ca7281eda150677a3b0`.
This check is included in `npm run verify`. **This normal corpus has not been
timed in C#, on the old CPU, or in the browser**; the earlier 1,050 exploratory
browser samples use the separate 21-case seed set. No performance acceptance
or speedup follows from freezing and parsing this future benchmark workload.
