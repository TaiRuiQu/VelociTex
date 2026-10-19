# VelociTex

English | [中文](README.zh-CN.md)

A C# math typesetting and rendering library for native Windows apps. It aims to reproduce the **core math typesetting of KaTeX 0.19.0** on WinUI 3, export PNG / SVG / MathML, and replace the formula display path of `../oh-my-study/` as the first priority.

**Status as of 2026-10-19: requirements are confirmed and the implementation design is complete. There is no rendering engine yet, no installable NuGet package, and no evidence that the performance targets are met.** The APIs, project layout, phases, and budgets below are designs to be built, not shipped features.

## Confirmed Direction

- C# / .NET 10; Windows 10/11 x64 for the first release, keeping Native AOT compatibility.
- The math core stays independent of WinUI; native drawing uses the system DirectWrite / Direct2D, with lightweight C# bindings allowed.
- Scope is limited to KaTeX 0.19.0 core math, including user macros and grouped scopes. Browser features and extensions are out of scope.
- Formulas may use the original KaTeX fonts. Anti-aliasing and minor font or spacing differences are accepted.
- The first version optimizes for typesetting quality and easy integration: once initialization is done and the formula cache misses, P95 from validating one formula to producing a drawable result is ≤10ms.
- Optimization continues afterwards toward a reproducible ≥10× speedup; that multiple is not a first-version gate.
- MIT licensed and released as an independent NuGet package; bundled fonts keep their own OFL-1.1 license.

## Documentation

| Document | Purpose |
| --- | --- |
| [Requirements and Scope](docs/requirements.md) ([中文](docs/requirements.zh-CN.md)) | User-confirmed goals, first-version scope, compatibility definition |
| [Architecture and Interfaces](docs/architecture.md) ([中文](docs/architecture.zh-CN.md)) | Compact data structures, pipeline, memory ownership, rendering and export |
| [Phased Implementation](docs/roadmap.md) ([中文](docs/roadmap.zh-CN.md)) | Dependencies, tasks, phase exits, pre-release versus stable |
| [Validation and Performance](docs/validation.md) ([中文](docs/validation.zh-CN.md)) | KaTeX comparison, visual thresholds, the exact 10ms definition, memory and speedup measurement |
| [Downstream Migration](docs/integration-oh-my-study.md) ([中文](docs/integration-oh-my-study.zh-CN.md)) | Integration points, inline host, migration and rollback |
| [Agent Handover](docs/agent-handoff.md) ([中文](docs/agent-handoff.zh-CN.md)) | How to start, definition of done, known risks |
| [Upstream Baseline](docs/reference/katex-0.19.0.lock.json) | Verified commit, release archive digest, 20 TTF digests |
| [Formula Seed Set](docs/examples/formulas.json) | Calculus, matrices, styling, macros, boundary and error cases |
| [Document Verification Record](docs/verification/design-review.2026-10-19.md) ([中文](docs/verification/design-review.2026-10-19.zh-CN.md)) | What was actually checked in this round: docs, upstream digests, formula samples |

Suggested reading order: requirements → architecture → implementation → validation → migration. The next step is P0: freeze the compatibility list, verify fonts, native drawing and AOT, and build a repeatably runnable comparison tool.
