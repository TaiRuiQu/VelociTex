# Compatibility, Visual, Performance, and Memory Acceptance

English | [中文](validation.zh-CN.md)

**Acceptance update, 2026-10-19:** the user explicitly allowed anti-aliasing differences and requested correct typesetting as the criterion. Raster edge/RGB thresholds below remain diagnostic history and do not block acceptance. Semantics, font/glyph identity and ordering, structural geometry, baseline/spacing, colors and clipping remain hard requirements. `verify-p4.ps1` retains original pixel metrics using `--layout-only`; mathematical differences and clipping still fail.

This document defines executable verification protocols. The 10ms target is a user-confirmed first-version threshold; every other number is an initial engineering budget to be frozen after P0 calibration with the reason recorded, not a measurement taken in this round.

## 1. Comparison Baseline and Artifacts

Use the KaTeX code, distribution package, and fonts recorded in the [lock list](reference/katex-0.19.0.lock.json); the browser, development runtime, OS, and tool versions go into the run manifest. Test tooling may use browsers in development and CI; the shipped runtime never does.

Each fixture output directory contains at least:

```text
run-manifest.json
compatibility.json
fixture-id/
  input.json
  reference.mathml
  reference.geometry.json
  reference.png
  native.layout.json
  native.png
  output.svg
  output.mathml
  diff.png
  metrics.json
summary.json
```

The JSON records the input, options, macro state, font and data checksums, logical size, baseline, key node positions, ink bounds, results, and failure reasons. Screenshots, packages, and source versions are cross-correlated, so stale screenshots can never prove that new source code passed.

## 2. Compatibility List and Corpus

Extract the list from the fixed version's registry, symbols, macros, and transitive dependencies, recording both independent implementations and aliases. Each item carries a status: included or excluded with a reason, plus semantic, geometric, MathML, native, PNG, and SVG state. Release reports keep the original denominator.

Corpus sources:

- The [formula seed set](examples/formulas.json) in this repository.
- In-scope cases from upstream [katex-spec](https://github.com/KaTeX/KaTeX/blob/v0.19.0/test/katex-spec.ts), [mathml-spec](https://github.com/KaTeX/KaTeX/blob/v0.19.0/test/mathml-spec.ts), and [screenshotter-spec](https://github.com/KaTeX/KaTeX/blob/v0.19.0/test/screenshotter-spec.ts).
- Generated coverage per command, symbol, environment, and major option, covering semantic branches and interactions rather than only the happy path.
- Combination cases: calculus, matrices and arrays, nested super- and subscripts, stretchy symbols, special boxes, macro scopes, English and Chinese text.
- Negative cases: invalid input, exceeded limits, macro loops, cancellation, concurrent isolation, and repeated resource lifecycles.

The seed set only gets the tooling started; it is never proof of complete compatibility. Browser and extension cases from upstream are explicitly excluded, and unimplemented math commands must never be reclassified as extensions.

## 3. Semantic and MathML Hard Thresholds

- For the same input and options, the success / failure classification matches the scope and the published resource limits.
- Core node types, grouping, glyph and font selection, arguments, environment row and column counts, and macro expansion results are correct.
- Missing glyphs, lost nodes, wrong ordering, misattached super- or subscripts, a missing denominator or radical line, and a matrix with missing rows or columns are all failures, regardless of screenshot similarity scores.
- Ordinary errors carry stable error codes and source positions, and failure or cancellation never damages the engine, the workspace, or a macro session.
- MathML is compared by XML namespace and normalized tree, ignoring attribute order and meaningless serialization differences while preserving text, structure, operators, and display semantics.
- Do not collapse all whitespace uniformly: whitespace inside `mtext` is semantic, and every allowed difference must name the node and the rule.
- Copying yields the complete original text, and default isolated macros, explicit sessions, and cache hits are verified according to the contract.

Formal core compatibility requires every in-scope registered item to be covered and all of the hard thresholds above to pass at 100%.

## 4. Geometric and Visual Acceptance

### 4.1 Reference End Correct Measurement

Measure through real browser layout, not jsdom dimensions or `renderToString` estimates. Load the exact CSS and fonts, wait for `document.fonts.ready`, and confirm that every font in use has loaded. Fix the viewport, DPI, zoom, theme, and font fallback environment, and disable animations.

The formula's own em equals the native `FontSizeDip`; KaTeX's outer 1.21× font scaling must be handled so that it is not applied twice. MathML accessibility nodes do not take part in the visual bounding box. Use the baseline probe to measure the actual baseline and compare the logical box with the ink bounds; tests with labels set the same container width explicitly.

Reference node geometry comes from the measured DOM and glyph ranges. Where necessary, add node mapping in a fixed-version test build, but never change styles or layout. Generated rule data may explain results; it can never be used to verify itself.

### 4.2 Initial Tolerance

The tolerances below are derived from the formula's effective em and DIP, and are frozen after P0 calibration against single glyphs, known-correct hand-built cases, and the noise floor of the reference side:

| Comparison Item | Engineering Initial Tolerance |
| --- | --- |
| Baseline, math axis, major subnode positions | `max(0.02em, 0.35 DIP)` |
| Fraction lines, radical lines, minimum super- and subscript gaps, and similar | `max(0.015em, 0.25 DIP)`, while also satisfying non-intersection and other geometric relations |
| Total advance / height / depth | `max(0.04em, 0.75 DIP)`; long arrays additionally check per-column deviation, so the total tolerance cannot simply be widened |
| Clipping, missing glyphs, missing paths, matrix row / column errors | Not allowed |
| Raster edges | At least 95% of foreground edge points have a bidirectional distance within `max(1 device pixel, the pixels corresponding to 0.5 DIP)`; reported per category |

Align by baseline and declared coordinates first; per-formula translation, scaling, or deformation to raise a score is not allowed. The only global screenshot transform permitted is mapping the recorded origin of the same formula onto a common canvas.

Edge metrics, SSIM, and pixel differences are diagnostic evidence and must not let a large background dilute real errors. Neither a pixel similarity rate nor a "95% of fixtures pass" rate may be read as "95% KaTeX compatibility." Cases that exceed the automatic tolerance require a review of the reason, followed by a fix or a recorded, reviewed anti-aliasing or system-fallback difference; unresolved math typesetting errors are never left behind.

P0 may adjust the initial thresholds uniformly, and the adjustment evidence must be kept. Afterwards, thresholds must not be relaxed case by case to make failures disappear. Pass statistics are computed per category, so rare categories cannot be masked by a large number of simple formulas.

### 4.3 Scenario Matrix

- Inline and display styles, explicitly covering each math style and the cramped branches.
- Font sizes of at least 16 / 24 / 32 DIP and 100 / 125 / 150 / 200% DPI.
- Dark and light themes, explicit colors, and transparent PNG on both dark and light backgrounds.
- Empty input, ordinary, complex, very long, invalid, and over-limit inputs.
- Small and large matrices, negative spacing, overlap, phantom, stretchy paths, and large fractions.
- The same size rendered as PNG and SVG in a controlled viewer, with the SVG opening even without installed fonts.
- The real WinUI control next to body text and punctuation, in wide and narrow windows, with scrolling and unload / reload.

After every UI change, run the real application, capture automatic screenshots, and review the affected screens by hand. Diagrams, off-screen engine images, or a Computer Use session cannot substitute for real host screenshots. Pull requests may run a frozen layered subset; a release candidate runs the full matrix; expensive checks unrelated to the new change and already passed are not repeated without reason.

## 5. Precise Definition of 10ms

The timed region runs from a complete formula in memory through option and resource validation, lexing, macro expansion, semantics, layout, and finally complete drawing instructions plus the immutable result. It includes the text shaping and result allocation that are genuinely required, which must not be deferred to a "render preparation outside timing" phase.

Preconditions: fonts, rule data, and the graphics device are initialized; the JIT test is warmed up or the run uses AOT; the formula, semantic, and layout result caches are disabled. Long-lived font resources and static tables may stay resident, and the font and glyph lookup strategy is recorded in the manifest. Cold and hot glyph or text shaping caches are reported separately.

Threshold: on a frozen, representative, normal corpus, both overall and per major category, **P95 ≤10ms**. Report P50, P95, P99, the maximum, and allocation at the same time. Oversized stress inputs are reported separately and are never mixed into the normal corpus, and timing outliers are never deleted.

The normal corpus should cover short formulas, standard fractions and radicals, integrals and sums, aligned environments, 3×3 and 5×5 matrices, moderate nesting, and short text. P0 freezes the exact set and its input distribution so that simple formulas cannot be cherry-picked later.

Measure from an independent Release AOT benchmark host with a monotonic high-resolution clock: at least five independent runs, and at least 1,000 complete-call samples for every major category. Shuffle the formula order inside one persistent process so repeated identical formulas cannot fake a cache miss, and keep the raw samples.

Clearing previous-round state and warming up before measurement is allowed. During timing, GC must not be forced per call, and GC pauses must not be hidden. Core synchronous timing is separated from host background queue latency; scheduling, native prepare, draw submit, present, and encoding are each timed separately.

Without a real measurement on the old CPU, the 10ms acceptance only holds for the tested machines. A suggested reference machine is a 2015–2017 common dual-core / four-thread low-power x64 with integrated graphics; record the model and power policy, and never impersonate an old CPU by limiting core counts in a VM.

## 6. Other Latency and Memory Budgets

The following are initial engineering targets for P0 calibration, not new user-set hard thresholds:

| Item | Initial Budget / Reporting Method |
| --- | --- |
| Library first initialization | Rules, fonts, and devices are reported separately; explore a ≤150ms budget initially, excluding the whole WinUI process startup |
| One question with 10 formulas | No formula cache and complete host integration, initially targeting P95 ≤50ms |
| One page with 100 formulas | Report first visible results and all results separately; initially target P95 ≤250ms once all CPUs are ready |
| UI thread | A single submission ≤4ms where possible, splitting long batches; the real frame rate and present time are listed separately |
| Export | Report by size, glyph count; suggest normal formula PNG ≤20ms, SVG ≤10ms, MathML ≤5ms, freeze after P0 |
| Result and resource cache | 16MiB per engine by default, stating explicitly which native resources are included; GPU surfaces are listed separately |
| Workspace buffer | At most two retained workspaces by default; a single temporary capacity above roughly 1MiB does not stay resident |
| Incremental memory for a typical 100 formulas | After font initialization, target an initial private-memory increment of ≤32MiB on an existing host, reporting the managed heap and native / GPU memory alongside it |

If P0 finds a particular exploratory budget unrealistic, record the reason, the workload, and the replacement value. Targets must not be met by cutting functionality or by moving a required phase outside the timing region.

Memory tests cover: clearing content, 1 / 10 / 100 / 1000 formulas, repeatedly opening and closing pages, going from a large formula back to a small one, cancellation and errors, theme and DPI switches, and creating and releasing across threads. Report total allocation, surviving memory, private bytes, commit size, native objects and handles, and GPU resources. `GC.GetAllocatedBytesForCurrentThread` only covers the synchronous managed path; it cannot stand in for whole-process or native zero allocation.

Caches are bounded by both bytes and item count, not by formula count alone. Fixed font data, input source text, result arrays, outlines, bitmaps and surfaces, and temporary encoding buffers all enter the ledger, so nothing is double-counted through duplicate references or left out entirely.

## 7. KaTeX Performance Comparison and ≥10× Rules

Establish three mutually independent result sets:

1. **Corresponding core sub-stages.** Use fixed-version test instrumentation to compare lexing, macro expansion, and parsing on the same workload. KaTeX's virtual HTML construction is not fully equivalent to this library's layout or drawing instructions, so the boundaries are marked separately.
2. **Warmed complete visual work.** On the same machine with the same formulas, effective font size, DPI, fonts, and cache state, the browser runs from calling KaTeX to finished DOM layout and drawing while the native side runs from the call to the corresponding layout and drawing. Measure CPU time, wall time, submit, and actual presentation evidence separately.
3. **Actual downstream experience.** Compare against the original Typst pipeline on problem splitting, first screen, full page, theme and DPI changes, and memory. This is migration benefit, not an algorithmic multiple relative to KaTeX.

`renderToString` only produces markup; browser layout and drawing have not happened. It cannot be mixed with the native complete presentation as an equivalent pipeline, and its speed cannot by itself justify an end-to-end multiple. What the reference side is actually capable of is described in the [KaTeX API](https://katex.org/docs/api).

A discussion of warmed-up algorithm or rendering speedups is only possible once browser and application startup are excluded on both sides. A C# cache hit cannot be compared with an uncached JS run, and native glyph-only drawing cannot be compared with browser parsing plus layout. Geometric correctness and complete output must both meet their thresholds.

Browser calculation time must not include two `requestAnimationFrame`-spanning display sync waits; separate CPU, GPU, and frame waiting with a browser trace, Windows ETW or present events, or verified measurement tooling. Screenshot capture happens outside the timed region to prove correctness, and screenshot encoding is never charged to one side.

A claimable ≥10× must name the exact metric and workload, give the frozen corpus distribution, per-category ratios, confidence or stability evidence, and the memory cost. When a local stage reaches ≥10×, only that stage may be claimed; if the end-to-end result is still bounded by the frame period, that limitation keeps being reported separately instead of being generalized into an overall multiple.

### 7.1 Single, complex, and multi-formula page workloads

A performance claim must cover more than a single short formula. At minimum, run
the frozen 60-formula normal corpus, a complex subset (5×5 matrices, aligned
systems, integrals/sums, nested fractions) and a deterministic 100-formula page.
Measure both call-to-completed-DOM/drawing and painted/presented frames, and
record the observed frame interval whenever a frame-bound number is reported.
Native and browser frame periods may differ; divide by the measured interval
before comparing presentation ratios. The shared harnesses and exact commands
are documented in `benchmarks/README.md`.

On 2026-10-20 the user added three explicit performance goals for this scope:
pure parsing to SVG/MathML at least 3× faster than KaTeX, WinUI end-to-end at
least 2× faster than browser KaTeX, and the WASM binding at least 50% faster than
browser KaTeX. The measured status and unmet targets are recorded in
`docs/verification/performance-2026-10-20.md`.

## 8. AOT, Stability, and Package Consumption

- Run display, PNG, SVG, MathML, font loading, and COM callbacks in both JIT and a real Native AOT consumer program; `IsAotCompatible` or a successful build never substitutes for running it.
- Fix rather than hide IL and trimming warnings, and record every remaining issue. Verify package dependencies, resources, licenses, target framework, and minimum system version.
- Randomized and generated inputs verify that nothing crashes, that resources stay bounded, and that cancellation can terminate; cover size overflow, release, threading, and device invalidation at native and unsafe boundaries.
- Regression invariants: layout stays consistent under proportional scaling; theme changes do not affect geometry; inline and display differences follow the rules; stateless concurrent results agree; after a failure the next ordinary formula still renders.
- Consume the nupkg from a local feed in a clean project and confirm that it does not merely work because of a project reference, a font from the development directory, or installed tools.

Every delivery reports three states: pass, fail, or not executed. Items that were not executed are never filled in with predicted values or with results from another machine.
