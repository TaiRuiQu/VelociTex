# Agent Handover Instructions

English | [中文](agent-handoff.zh-CN.md)

Updated on 2026-10-19 after the [P2 delivery](verification/p2-2026-10-19.md). The user explicitly requested P2 through P5 and one Git commit per completed phase. Continue with P3 downstream migration and P4 full core compatibility; P5 is still pending their real acceptance. P0/P1 and the requirements interview must not be repeated. Sections describing the original design-only round below are historical, not current status. P2 has local preview packages, 307 valid/31 invalid inputs, 491 JIT/AOT hard differential scenarios, 1473 identical exports and real isolated package consumers. Full compatibility, old CPU, SourceLink and publication remain unverified. The downstream working tree already contains user changes; preserve them and save touched baselines before editing. No remote or external publication is authorized.

## 1. Already Determined, Should Not Repeatedly Ask

- C# / .NET 10, WinUI 3, Windows 10/11 x64, Native AOT, with lightweight bindings allowed for DirectWrite / Direct2D.
- The only compatibility target is KaTeX 0.19.0 core math, including user macros and scopes, excluding browser features and contrib extensions.
- Formulas may use the original KaTeX math fonts; body text stays sans-serif; anti-aliasing and minor typesetting differences are accepted.
- The basic architecture is redesigned, upstream math rules are preserved, and compact data structures plus reusable workspaces are used.
- Native display, PNG, SVG, and MathML are required, along with copying complete formulas and basic accessibility; no editor and no streaming updates.
- The first-version core centers on a single complete formula; body-text mixing and automatic line breaking mainly stay with the downstream host, and an optional layer may be extracted later.
- After initialization is complete and without the formula result cache, P95 from validating one formula to producing a complete drawing result is ≤10ms; ≥10× is a later optimization goal.
- A typical question has fewer than 10 formulas and a page has 50–100; the target machine is roughly a decade-old x64 CPU with low memory usage.
- MIT open-source NuGet, prioritizing the replacement of the oh-my-study display chain while the existing print chain is temporarily retained.

The original confirmation and scope details are in [Requirements and Scope](requirements.md). Engineering budgets, specific dependencies, and visual thresholds are calibrated against P0 evidence and must not be presented as user-specified values.

## 2. Work Completed This Round

- Read the downstream AGENTS, the .NET / WinUI / AOT configuration, the native reading service, the Typst reading service, and the recent formula acceptance records.
- Verified the KaTeX `v0.19.0` git reference and npm metadata, downloaded the npm archive into memory, and verified SHA-512 and SHA-256.
- Checked the 20 TTF checksums and name tables: the fonts declare OFL-1.1 and RFN, so they cannot be marked MIT alongside the code.
- Wrote the requirements, architecture, roadmap, validation, migration, and this handover document, and supplied the upstream lock data and formula seed set.
- Verified the formula seed set against exact KaTeX 0.19.0: 21 valid formulas rendered successfully, 5 error formulas threw parse errors as expected, and 3 out-of-scope items were not counted as compatibility cases. See the [verification record for this round](verification/design-review.2026-10-19.md).

**No math engine, project skeleton, generators, WinUI controls, or NuGet package exist yet, and there is no evidence that this library passes the performance, visual, or AOT bars.** Nothing downstream was modified, and no Git repository, commit, or external publication was created.

The upstream archive was not saved in the repository and the fonts were not vendored, so the next phase must re-acquire and verify them against the lock data. The directory may still not be under Git at that point; handle it according to the user's working style and do not assume any existing branch or remote.

## 3. Reading Order

1. [Requirements and Scope](requirements.md).
2. [Architecture and Interfaces](architecture.md).
3. [Phased Implementation](roadmap.md).
4. [Validation and Performance](validation.md).
5. [Downstream Migration](integration-oh-my-study.md).

Advance autonomously on ordinary implementation choices inside a phase. Ask again only when a new requirement conflicts, when confirmed scope must change, or when an external publishing identity is needed.

## 4. First Implementation Task

P0 through P2 have implemented tools and real checks. P2 passes 307 valid / 31 invalid fixtures, 491 geometry/MathML scenarios per runtime, actual JIT/AOT exports, and real package-only headless and WinUI AOT consumers. Raster exceptions remain explicitly classified. Next execute **P3-01 through P3-05** while the independent **P4 remaining-core work** advances; then close all real P5 gates. Read the P2 record and package contract, extend the compact architecture, and preserve existing downstream user edits. Layout scratch arrays and drawing decorations still allocate per compile; report these honestly and require comparison evidence for reuse optimizations. Reuse the established loop:

```text
Verify upstream
  → Reproducibly generate the core list and fixed sample reference
  → Native / AOT private fonts with glyph and path drawing
  → PNG / SVG probe output and real WinUI screenshots
  → Prove the inline baseline host solution
  → Freeze the technical dependencies and calibrate the budgets
```

Do not implement every command up front, and do not stay at abstract interfaces, empty projects, or claims of "theoretically feasible." P0 probes may use hard-coded drawing data, but they must be labelled as probes and never count toward formula compatibility coverage.

The expected repository layout is below; the actual projects are created during implementation:

```text
VelociTex.slnx
global.json
Directory.Build.props
Directory.Packages.props
src/
  VelociTex.Core/
  VelociTex.Rendering.Direct2D/
  VelociTex.WinUI/
tools/
  VelociTex.DataGeneration/
  reference/
compatibility/
tests/
  VelociTex.Core.Tests/
  VelociTex.Layout.Tests/
  VelociTex.Export.Tests/
  VelociTex.Native.Tests/
benchmarks/
samples/
  VelociTex.WinUI.Sample/
docs/
artifacts/  # large runtime artifacts; the ignore policy is defined during project setup
```

Runnable `build.ps1`, `verify-reference.ps1`, `verify-p1.ps1`, actual engine tests and native/reference probes exist; see the P1 delivery for commands. Every later delivery must keep providing real, reproducibly runnable commands.

## 5. Technical Risks and Solutions

| Risk | Response and Completion Evidence |
| --- | --- |
| TTF, KaTeX metrics, and the real browser advance are not fully consistent | Calibrate single glyphs and styles in P0, derive geometry from both source rules and real measurements, and never casually change metrics |
| WinUI or binding versions hit resource and callback problems under AOT | Publish and run a minimal real consumer first, then lock the version once the dependency is chosen |
| Standalone formulas work but the mixed replacement regresses | Prove the baseline host in P0, and make P3 cover body text, punctuation, and tables |
| Macro side effects collide with the cache | Isolate by default, make sessions explicit and commit on success, and disable result caching for inputs with write operations |
| SVG disagrees with the native fonts and paths | Export from the same drawing result, lock the path fonts and checksums, and open and inspect the output independently |
| Self-drawing breaks screen reading or copying | Give MathView a basic AutomationPeer plus keyboard and original-text copying; MathML does not serve as the UIA implementation |
| No real measurement machine for the old CPU | Keep implementing but keep the item unverified; modern-machine predictions never count as meeting the threshold |
| Extreme formulas pollute the pool or explode image sizes | Bound the stack, node count, cache, and export pixels, keep large buffers non-resident, and test recovery after errors |
| The comparison corpus only contains simple formulas | Keep the complete functionality denominator, report statistics per category, and include special boxes, CD, labels, macros, and errors |
| Later optimizations trade accuracy for a multiple | Run differential, memory, and AOT checks with every optimization, and claim only measured results under equivalent workloads |

## 6. Phase Delivery Format

Each delivery updates a short maintenance record containing at least:

- The completed phases and work packages, the real behavior, and the interfaces involved.
- The source version of this round, the dependency / font / generated-data checksums, and the reproduction commands.
- Items that passed, failed, or were not executed, plus the locations of raw screenshot, differential, and timing data.
- Compatibility list changes, known differences, and remaining work.
- The smallest closed loop to do next.

Without real screenshots, UI acceptance must not be claimed; without an old CPU, a real AOT consumer, or a published package test, each of those items must be stated explicitly. Historical evidence may be reused, but any part affected by a new change must be updated.

## 7. Authorization and Downstream Boundaries

This round authorized the delivery of requirements and design documents. Future implementation work runs within whatever the user authorizes at that time; this roadmap is not extra authorization to modify the downstream repository, create a remote, or publish externally.

Before touching oh-my-study, read its latest AGENTS.md first. The math-font exception confirmed in this Q&A has to be passed on explicitly and must not be overridden by the older document's blanket sans-serif rule. The downstream also requires automatic screenshots and real inspection after every UI change, and that must be respected.
