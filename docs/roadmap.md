# Phased Implementation Roadmap

English | [中文](roadmap.zh-CN.md)

This document delivers "replace the display chain early" and "reach complete core compatibility" as separate milestones. P0 and the P1 basic pipeline were delivered on 2026-10-19; see the [P1 record](verification/p1-2026-10-19.md) for actual checks, reviewed raster exceptions and unverified hardware/measurement items. P2 is next. Phases advance by dependency, never by an unverified fixed schedule.

## Phase Overview

| Phase | Goal | Dependencies | Deliverable |
| --- | --- | --- | --- |
| P0 | Reference baseline, native / AOT feasibility, and acceptance tools | Confirmed requirements | Upstream list, runnable native probe, comparison pipeline, budget calibration |
| P1 | Compact core skeleton and minimal vertical closed loop | P0 | Formula → data layout → native / PNG / SVG / MathML |
| P2 | Common math pre-release version | P1 | Integrals, matrices, and other common formulas; integrable local NuGet |
| P3 | Prioritize downstream display replacement | P2, paragraph host verified | oh-my-study native display migration and rollback acceptance |
| P4 | Complete all core within scope | P2; migration feedback enters this phase | Full core compatibility and differential report |
| P5 | Formal open-source library delivery preparation | P3 + P4 | 1.0 candidate, local package, consumption verification, documentation and licenses |
| P6 | Continuous performance optimization | Reproducible correctness and performance baseline | Auditable optimization results, gradually exploring ≥10× |
| P7 | Optional extension layers | Requirements to be determined separately | Paragraph mixing library, other platforms or outputs, etc. |

P3 can consume the pre-release version first; there is no need to wait for P4 to gain real value. While P4 is incomplete, "complete KaTeX compatibility" must not be claimed. Keep the phases serial and closed-loop, and avoid writing large amounts of code that has no verification entry point.

## P0: Freeze Baseline, Connect Verification and Native Probe

### Work Packages

- **P0-01 Upstream acquisition.** Verify the git commit and npm archive against the [lock list](reference/katex-0.19.0.lock.json), and record checksums for source files, CSS, TTF, WOFF2, and the font mapping. Lock the generator's development dependencies and browser versions.
- **P0-02 Compatibility list.** Enumerate commands, symbols, built-in macros, environments, and options through the AST and registry, completing the picture through transitive dependencies. Produce `compatibility/core-0.19.0.json`; every item records its source, whether it is included, the reason for exclusion, its semantic / visual / MathML status, and a fixture ID. Extension and browser classifications get a manual review.
- **P0-03 Font verification.** Verify the 20 TTFs, the OFL / RFN declarations, cmap and glyph IDs, TeX metrics, and TTF advances; measure the real impact of CSS on symbol size and baseline. Build single-glyph, italic-correction, math-axis, and baseline calibration charts.
- **P0-04 Native probe.** A minimal C# / AOT WinUI program loads private fonts, draws explicit glyph runs, radical paths, and rectangular rules, and exports transparent PNG and standalone SVG. Verify 100/125/150/200% DPI, dark and light themes, WARP, unload / reload, and device resource reconstruction.
- **P0-05 Selection.** Verify the lightweight bindings and Composition surfaces. If critical callbacks, AOT, or DPI turn out to be unworkable, compare a Win2D surface. Record the actual selection, version, publish parameters, and maintenance risks in an ADR, then keep only one implementation path.
- **P0-06 Baseline comparison.** With exactly 0.19.0, generate normalized semantic results, MathML, browser geometric anchors, and screenshots. Browser samples are taken after fonts load, starting with the [formula seed set](examples/formulas.json). Render the reference side against itself repeatedly to measure tool noise.
- **P0-07 Paragraph baseline probe.** Verify the WinUI host's baseline, line height, and line breaking with body text containing formulas, fractions, matrices, and punctuation. First check whether RichTextBlock's InlineUIContainer meets the requirement; if the baseline cannot be expressed precisely, adopt the DirectWrite inline object host and state its copy / accessibility behavior and its effect on existing body text.
- **P0-08 Budget and hardware.** Fix the development machine, name the old-CPU reference machine or mark it unverified, measure the original Typst pipeline and warmed-up KaTeX, and calibrate the memory and phase budgets. Record CPU / GPU / OS / DPI / power policy and software versions.

### Exit

Run the AOT probe for real in a fresh process, proving that fonts, native drawing, the two image exports, and the UI host route are feasible; generate the reference results reproducibly; leave no unclassified item in the scope list. P0 does not require a complete TeX parser, and a hard-coded formula probe cannot stand in for the real engine.

If native interop causes trouble, solve it or change the selection inside this phase; do not carry infeasible dependencies into P2. If the old hardware is temporarily unavailable, keep that item marked unverified and keep developing, but do not claim the old-CPU performance threshold is met.

## P1: Basic Architecture and Minimal Complete Pipeline

### Work Packages

- **P1-01 Project skeleton.** Establish the slnx solution, shared build props, centrally managed package versions, lock files, Nullable and analyzers, the Core / Windows / WinUI projects, tests, and samples. Align the toolchain with the downstream verified .NET 10, and do not swap the host SDK for an unverified version.
- **P1-02 Data and containers.** Generate static font and symbol tables; add source slices, compact nodes, payloads, explicit stacks, the workspace, and resource budgets.
- **P1-03 Lexing and syntax.** Basic symbols, groups, superscripts and subscripts, basic macro expansion, and mode / style switching; structured source-range diagnostics and resource limits.
- **P1-04 Layout.** Atom spacing and Bin reclassification, fonts, super- and subscripts, simple fractions, radicals, natural width, baseline, and ink bounds.
- **P1-05 Homologous output.** An immutable MathDocument, native glyph / rule / path drawing, PNG / SVG / MathML; invalid input keeps the original text and returns diagnostics.
- **P1-06 Lifecycle.** Workspace isolation, results that do not borrow temporary buffers, thread and device resource release, cancellation, and error rollback.

### Exit

At least 50 fixtures covering the basic rules pass semantic, geometric, and output checks in both their valid and invalid forms. One formula produces identical semantics and geometry across all four outputs, a real AOT program runs, and the exports are actually opened and inspected. The hot path allocates no per-token or per-node classes, and allocation and native resources have a baseline report.

Start using the final data architecture in this phase and grow the layout rules gradually; do not build a throwaway DOM engine first and rewrite later.

## P2: Common Math Integrable Pre-release Version

### Work Packages

- **P2-01 Common math.** Complete integrals, sums, limits, generalized fractions, binomials, stretchy delimiters, accents, arrows, fonts, and math text.
- **P2-02 Environments.** The matrix family, array, cases, aligned, and other common environments, plus column spacing and array rule lines.
- **P2-03 Macros and options.** User macros, local scopes, common definitions and arguments, inline and display styles; explicitly list the remaining items from the complete list.
- **P2-04 Controls.** MathView, binding of pre-compiled results, theme and DPI invalidation, copying the complete formula, and a basic AutomationPeer; no formula editor.
- **P2-05 Representative workload.** 1 / 10 / 50 / 100 formulas, cache miss and hit, complex integrals and matrices, measured against a real AOT consumer.
- **P2-06 Package and interface.** Build a local pre-release nupkg, ship non-WinUI export samples and WinUI display samples, and freeze the first round of public interfaces.

### Exit

The representative normal workload reaches a single-compilation P95 ≤10ms, and at least 300 frozen, category-distributed fixtures meet this phase's visual threshold. All three exports are available, and the copy and screen-reader basics pass. Unsupported items return explicit diagnostics, and cache and queue sizes stay bounded.

If the old machine cannot be measured, a pre-release package may be labelled "awaiting old-hardware acceptance"; full performance acceptance must not be claimed. Native prepare, submit, and actual render times are reported separately, and layout must never be quietly moved outside the core timing.

## P3: Migrate oh-my-study Display Chain

Execute the [migration plan](integration-oh-my-study.md). First record the formula font exception in the downstream conventions, then integrate the local package or a pinned pre-release version.

### Work Packages

- **P3-01 Call boundary.** Keep the existing math delimiter scanning and domain objects, hand a single formula to VelociTex, and replace the display stage's LatexFormulaConverter / Typst dependency.
- **P3-02 Host paragraph.** Use the route chosen in P0 to handle body text, inline formulas, baseline, line breaking, and display blocks. This layer belongs to the downstream host, not to Core.
- **P3-03 Scenario completeness.** Body text, options, tables, sub-questions, feedback, and list previews all integrate consistently. Keep the original text on errors, generation and cancellation protection, and scrolling for long formulas.
- **P3-04 Visual and flow.** Automatically capture screenshots of the real AOT application across themes, DPI, wide and narrow windows, and cache states, and check the surrounding areas. Keep regression coverage for the existing print / PDF path.
- **P3-05 Measurement and switching.** Measure latency and memory before and after with the same sample, cache state, and build. Switch the default display path to the new library, keep a time-boxed internal rollback switch, and remove the old display-specific code once stability is confirmed.

### Exit

The downstream default display path uses the new library, the target scenarios have run with screenshot evidence, and ordinary valid math display no longer calls Typst. Printing may still depend on Typst, so the whole toolchain must not be deleted on that basis.

If only standalone formulas were replaced while mixed paragraphs still use Typst, record the state as a "partial migration" and finish P3; do not announce it as a complete display replacement.

## P4: Full Core Compatibility

### Work Packages

- Complete the remaining core commands, macro behaviors, Unicode mappings, special boxes, size units, labels and numbering, and rare environments from the list.
- Complete the continuous macro session, error ranges, and the diagnostic strategy; verify nesting, scope recovery, and the differences in failure rollback.
- Normalized MathML comparison covers every in-scope node; complete exported colors, path transforms, and special geometry.
- Expand the corpus from upstream tests, the support documentation, and generated use cases; aim for at least 1,000 independent valid math fixtures, including negative cases and extreme but valid inputs.
- Every in-scope registered item has semantics plus at least one typesetting or output use case; symbol coverage is generated per font and atom category instead of repeating the same formula in screenshots to pad the numbers.

### Exit

100% coverage of in-scope functionality, with all math-content correctness hard checks passing. Visual checks reach the [validation thresholds](validation.md), and there are no unresolved missing glyphs, wrong ordering, clipping, baseline, or structural issues. Every exception has an auditable reason; failures cannot be cleared by moving unimplemented items out of scope.

## P5: 1.0 Candidate and Release Preparation

- The public API is stable, with XML documentation, error codes, resource limits, and thread and lifecycle contracts complete.
- The MIT library license, the KaTeX MIT license, the font OFL / RFN terms, the source list, and dependency notes ship with the package; the package contains no development browser or Node dependencies.
- SourceLink, symbol packages, deterministic builds, package verification, compatibility notes, security and input boundaries, and the changelog are complete.
- Restore from the local feed in a clean consumer project, build both JIT and Native AOT, run them for real, and cover native display, PNG, SVG, MathML, font loading, and basic accessibility.
- Downstream default path acceptance, old-CPU performance evidence, and memory pressure, cancellation, device invalidation, and DPI switching evidence are complete, with platform notes explained rather than dropped.
- Freeze the 1.0 candidate nupkg, the report, and the reproduction commands. Once the package name and repository identity are confirmed, publish externally under the user's authorization.

## P6: Continuous Optimization and ≥10× Exploration

Prioritize optimizations that do not change typesetting:

1. Continuous data for parsing / macro hotspots, reduce strings and repeated traversals.
2. Workspace warm reuse, fixed-size output writes, reduce copies and large object retention.
3. Bounded reuse of glyph and text shaping; correctly handle macro side effects when reusing.
4. Merge adjacent glyph runs, path reuse, reduce native calls and surface update regions.
5. Batch layout, downstream visible content scheduling, and caching; prevent queue and concurrent memory growth.
6. Introduce SIMD / SoA specialized layout only when provably beneficial; provide scalar fallback for old CPUs.

Each optimization requires before / after semantics and geometry, a latency distribution, managed and native allocation, and memory reports on the same corpus. A speedup multiple may only be claimed under equivalent workload, cache state, and warm-up conditions; browser startup or frame synchronization waits do not count as an algorithmic gain. The full definition of the goal lives in the validation document.

## P7: Subsequent Optional Layers

The paragraph host proven downstream can be extracted into an optional mixing module. Other platforms, ARM64, print / PDF, extended commands, and editing interactions all require a new requirements confirmation; they do not enter the first version by default.
