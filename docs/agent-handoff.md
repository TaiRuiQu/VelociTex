# Agent Handover Instructions

English | [中文](agent-handoff.zh-CN.md)

Updated on 2026-10-19 after P4 and local P5 candidate preparation. Read this as
current operational status. Original design-round work is historical and remains
in the [design review](verification/design-review.2026-10-19.md).

## Current delivery

P0–P4 are implemented and committed. P2 is `867283c`, P3 is `cd05074`, and P4 is
`1b0c777`. The local `1.0.0-rc.1` candidates use clean P4 source revision
`1b0c77774418fd349c36b8b4cd0c89a135f70fba`; isolated JIT/AOT, WinUI and downstream
consumption have actual process, package and screenshot evidence. See the
[P4 record](verification/p4-2026-10-19.md), [P5 record](verification/p5-2026-10-19.md)
and `docs/reference/p4.evidence.json` / `p5.evidence.json`.

All 2,939 included registration identities have source-observed executed use
cases. P4's 3,659 valid inputs and 50 errors pass 3,843 independent scenarios per
JIT/AOT runtime; 11,529 ordinary exports are byte identical. Registration use-case
coverage does not prove every possible argument, macro combination or platform.
The current immutable output and lifecycle contract is in
[package-contract.md](package-contract.md), not the historical architecture API
sketches.

The user explicitly accepts antialiasing differences when mathematical
layout is correct. Raw pixel diagnostics remain recorded; semantics, geometry,
missing glyphs, ordering, clipping and lost colors still fail acceptance.

## Requirements to preserve

- C#/.NET 10, WinUI 3, Windows x64 and Native AOT/trimming remain fixed. The only
  compatibility baseline is KaTeX 0.19.0 commit
  `6ea2dc97188f0cc282e05a9690418ae7fe5457ba`; never track main or upgrade it.
- Preserve upstream mathematical rules in compact native data, without a
  runtime DOM, JavaScript engine, browser, Node.js or Typst formula pipeline.
- Original KaTeX fonts are permitted for formulas. Body text stays sans-serif;
  the fonts retain separate OFL/RFN terms and are not MIT.
- Core handles one complete formula. Downstream DirectWrite paragraph wrapping
  and inline-object hosting do not become Core's paragraph responsibilities.
- Native display, PNG/SVG/MathML, complete-source copying and basic accessibility
  are required. A UIA peer is not a screen-reader audio test.
- The warmed complete cache-miss P95 target is 10 ms, including required shaping
  and independently owned result allocation. A later 10× goal is not achieved
  by the current measurements; modern hardware does not satisfy old-CPU gates.

## Remaining formal release work

Local candidate preparation is complete. Formal P5/1.0 exit is still pending:
old CPU and low-memory hardware, physical monitor DPI transitions, minimum
Windows 10 real-machine acceptance, hardware GPU/device invalidation and exact
native/GPU peak accounting, and screen-reader audio. SourceLink syntax and
embedded source checksums pass, but the local commit is not claimed remotely
available. There is no external publication or push authorization.

The frozen candidate also has 27 XML record-property members with an isolated
`>` text fragment. XML remains valid and summaries exist; fix the source comment
format and rebuild a new candidate before a stable release. Do not silently
replace frozen RC1 package bytes or its reviewed locks.

New implementation changes must retain hard differential tests, resource bounds,
JIT/AOT consumers and real-app screenshots after WinUI changes. Required layout
scratch/results/exports allocate; report managed and native costs honestly.

## Reproduction and reading

GitHub Actions Lint, version-tag-triggered NuGet Trusted Publishing and automatic
GitHub Release creation are configured
in `.github/workflows/`; see [setup instructions](github-ci.md) and the
[local CI verification record](verification/ci-2026-10-19.md). The user reports
the `nuget` environment and NuGet trust policy are configured; hosted execution
and actual publication remain unverified. CI configuration
does not change the frozen candidate or establish the outstanding formal gates.

Read requirements → package contract → P4/P5 records → validation → migration.
Use `verify-p4.ps1` for the final mathematical/reference checks and
`tools/Write-P5Evidence.ps1 -Check` to check the frozen local candidate evidence.
Rebuilding candidates needs the pinned SDK, native build tools and a clean source
checkout at the recorded revision; the P5 record separates rebuilding from
verifying existing artifacts. Large runtime evidence is in ignored `artifacts/`.

## Downstream and publication boundaries

Read `../oh-my-study/AGENTS.md` before touching that repository. It had substantial
user modifications before migration. `integration/oh-my-study/p3.patch` and
`p5.patch` are deltas against their captured dirty baselines, not patches for an
arbitrary clean checkout. Never stage or commit unrelated downstream user work.
The formula-font exception is recorded there; printing remains on its original
Typst path. Real automatic screenshots remain mandatory after UI changes.

The supplied repository identity is `TaiRuiQu/VelociTex`. A local pack, source map
or version string does not authorize remote publication, uploading, pushing or
signing a release. Keep unexecuted checks explicit rather than relabeling them
as complete.
