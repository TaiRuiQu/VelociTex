# VelociTex Requirements and Scope

English | [中文](requirements.zh-CN.md)

Confirmed date: 2026-10-19. Source of requirements: Current user Q&A session. This document distinguishes between "user-confirmed" items and "engineering proposals" to avoid conflating optimization ideas with product requirements during implementation.

Later confirmation on 2026-10-19: anti-aliasing differences are acceptable; correct mathematical typesetting is the acceptance criterion. Pixel-edge differences remain diagnostic, while semantic and layout correctness, colors, missing glyphs and clipping remain hard checks.

## 1. Product Goals (User Confirmed)

Build an independent mathematical typesetting and rendering library in C# for fast display of complete formulas in native WinUI 3 interfaces. The first downstream consumer is `../oh-my-study/`, with priority on replacing that project's existing formula display pipeline. The product will be delivered as an independent open-source NuGet library under MIT license.

The typesetting reference is locked to exactly one version, **KaTeX 0.19.0**. The goal is as close to 1:1 reproduction as possible, with at least roughly 95% of the intended visual result for typical formulas. Acceptance focuses on mathematical typesetting consistency and accepts anti-aliasing plus minor font and spacing differences. "95%" does not mean that 5% of commands may be dropped at will, and it is not a pixel-similarity ratio. Executable geometric and visual thresholds live in [Validation and Performance](validation.md).

Performance has two tiers: the first version restores fidelity while staying fast enough, and later versions pursue an order-of-magnitude speedup over KaTeX in the browser. The ≥10× target is not a first-version release requirement.

## 2. Platform and Runtime (User Confirmed)

| Item | Confirmed Boundary |
| --- | --- |
| Language / Platform | C#, .NET 10 |
| Initial Release | Windows 10/11 x64; minimum system baseline adopts downstream's existing Windows 10 19041 |
| Publishing | Native AOT compatible; JIT builds retained for debugging convenience |
| Native Display | WinUI 3; WebView or browser-based runtime formula rendering is prohibited |
| Graphics Dependencies | System DirectWrite / Direct2D with lightweight C# bindings allowed; specific bindings and display surfaces to be locked after P0 testing |
| Core Boundary | Math core does not depend on WinUI; platform-agnostic structures reserve interfaces for future reuse, but initial cross-platform support is not promised |
| CPU | Covers common x64 CPUs from approximately the last decade; AVX2, AVX-512, or modern discrete graphics are not prerequisites |
| ARM64 / Other .NET | Not required for initial release |

The WinUI host, the Windows App SDK, and the SDK toolchain versions must be verified against downstream lock files during P0. A package merely declaring support for a Windows version does not count as real-machine acceptance.

## 3. Math Compatibility Scope (User Confirmed)

In scope are the core math commands, symbols, styles, and math environments of that fixed version, plus user macros and grouped scopes. Both inline and display styles are covered; line breaks, matrices, alignment, and formula labels inside core environments belong to math layout.

Out of scope for now:

- HTML, DOM, CSS injection, and browser-specific behaviors.
- Links, external images, and other resource access commands such as `\href`, `\url`, `\includegraphics`.
- `mhchem`, automatic web page scanning, copy-tex, and other contrib extensions.
- Arbitrary LaTeX packages, complete TeX document compilation, and JS callback-style extension macros.
- Compatibility commitments to other KaTeX versions.

**Where the source code registers something does not decide its scope.** P0 extracts a complete list from the registry, symbol table, built-in macros, and environments, then annotates every item individually. Internal helper commands that core math depends on must keep their required semantics even when their names mention HTML; they cannot be deleted by file name. Math environments such as `CD` cannot be filed as extensions just because they are rarely used. The list is derived from the [0.19.0 function entry points](https://github.com/KaTeX/KaTeX/blob/v0.19.0/src/functions.ts), the [environment entry points](https://github.com/KaTeX/KaTeX/blob/v0.19.0/src/environments.ts), and their transitive registration dependencies.

The first integrable pre-release may cover the common subset and publish what remains. A formal claim of "complete core compatibility" is only allowed once every in-scope item is implemented and verified; inputs beyond the resource limits get explicit diagnostics and do not count as ordinary compatibility cases.

## 4. Fonts, Rendering, and Output (User Confirmed)

- Math formulas may use the original KaTeX fonts; surrounding body text stays sans-serif.
- Native display draws the result directly; **PNG, SVG, and MathML** must also be exported.
- Copying the complete formula source and basic accessibility are supported. Partial formula selection, continuous selection across body text and formulas, an in-formula caret, and link interaction are not required.
- Streaming input is not required; rendering starts once a complete formula has arrived.
- First-version core API: "validate a formula → return a drawable result, dimensions, baseline, and diagnostics."
- Mixing formulas into body text and paragraph line breaking are optional later capabilities; the downstream host usually owns them.
- The first migration keeps the existing print pipeline; this round does not replace PDF or print engines.

Engineering definition of copy and basic accessibility: the original text copies in full and copying is keyboard-triggerable; a formula is exposed as one semantic object carrying a name, the source text, and any description needed, so that the downstream host can supply natural-language descriptions. MathML export does not automatically equate to Windows screen reader support, and full spoken navigation of mathematical structure is not promised.

## 5. Workload and Response (User Confirmed)

| Dimension | Requirement |
| --- | --- |
| Per Question | Typically fewer than 10 formulas |
| Per Page | Approximately 50–100 formulas |
| Updates | Process after complete formulas arrive; no incremental token-by-token rendering |
| Latency | With fonts and devices initialized and no formula cache, a single formula from validation through layout to a drawable result is **P95 ≤10ms** |
| Other Timing | PNG / SVG / MathML export, first process startup, full-page rendering measured and budgeted separately |
| Memory | Minimize; cache and workspace buffers bounded, do not trade high memory for apparent speed |
| Corpus | Can generate representative complex formulas including calculus, matrices, etc.; does not depend on real user data |

The 10ms budget applies to a frozen, representative, normal workload; it does not require arbitrarily long extreme formulas to finish inside 10ms. Valid ranges, exceptional loads, and export size limits must be public. Specific hardware is not fixed yet: P0 establishes an approximately 2015–2017 low-power dual-core / four-thread x64 baseline, and without measurements on such hardware, results from a modern development machine cannot be reported as old-CPU acceptance.

## 6. Architecture Principles (User Confirmed)

Porting upstream algorithms, extracting data at build time, and differential verification are all acceptable, but the architecture underneath must be redesigned rather than translated line by line. Compact arrays, arenas, source slices, pooling, and later SIMD techniques may be used aggressively.

In practice: keep semantics and typesetting rules faithful, redesign storage and scheduling. Get correctness and the native closed loop working first, then optimize continuously against measurements; do not build large numbers of hard-to-optimize throwaway object trees and push all data-oriented refactoring to the end.

## 7. Decision Status

The user requirements are confirmed; there is no need to ask again about anything settled here. The following are technical decisions to be made during implementation, which the Agent completes with evidence inside the authorized scope:

- Actual selection of binding libraries and WinUI drawing surfaces.
- Visual thresholds calibrated during P0, phase-based memory and export budgets.
- Specific implementation of downstream native paragraph adaptation.
- NuGet package name availability, repository address, publishing identity, and signing configuration.

The last item does not affect local development, packaging, or acceptance. Creating a public repository, uploading, or publishing requires separate user authorization.
