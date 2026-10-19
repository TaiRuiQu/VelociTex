# P0-08 hardware and original pipeline baseline

Measured on 2026-10-19. The existing downstream reading service was called for
real from a separate local development harness. No downstream source, project,
settings, or publication was changed. Its latest `AGENTS.md` was read first.

## Hardware and software

`docs/reference/development-hardware.json` records the actual registry/native
inventory. CPU: Intel Core i5-12400F, 12 logical processors; physical memory:
34,160,545,792 bytes; GPU inventory: NVIDIA GeForce RTX 5060 Ti, driver
32.0.16.1692. OS APIs report build 26200, revision 9457, display version 25H2.
The legacy ProductName registry value says Windows 10 Pro and is preserved as
reported; it is not used to infer the actual marketing version.

The active power plan was Balanced. The PowerShell process reported 96 DPI and
2560×1440 on one monitor; process DPI virtualization is explicitly possible.
The real native screenshot/DPI probe remains authoritative for rendering. No
claim is made about controlled temperatures, fixed clocks, quiet background
load, isolated benchmark execution, or which GPU a renderer actually selected.

The harness used SDK 10.0.402 and ran on .NET 10.0.13. Typst 0.14.2 executable
SHA-256 is `1bfcabe2756657587c5f879041b55265c2a954c2ba8125ada842d2a33b371695`.
The existing Release downstream DLL SHA-256 is
`d0a92bbf001156d7bdef3eb788d95ce26be6ab50100ddac04cef6809c21f1e07`.
The initially inspected older probe Release DLL had no `RenderReadingAsync`;
the harness references the current `src/OhMyStudy.Documents/bin/x64/Release/`
DLL instead, without rebuilding it.

## Reproduction and boundary

```powershell
dotnet run --project tools/VelociTex.Baseline.Probe/VelociTex.Baseline.Probe.csproj -c Release -- C:\Users\XiaoQ\Documents\VelociTex C:\Users\XiaoQ\Documents\oh-my-study C:\Users\XiaoQ\Documents\VelociTex\artifacts\baseline 7
pwsh -File tools/VelociTex.Baseline.Probe/Write-HardwareManifest.ps1
pwsh -File tools/VelociTex.Baseline.Probe/Write-BaselineSummary.ps1
```

The managed filesystem sandbox initially caused the existing Typst process to
fail with `Access is denied (os error 5)`. The identical compiled harness was
then run outside the sandbox following automatic approval review and completed
successfully. The only writes were local synthetic evidence in this repository.
The development harness is excluded from the product solution and adds no
runtime dependency to the library.

Every call follows actual `RenderReadingAsync`: split delimiters, convert
LaTeX, write source, start a new Typst child, compile SVG, read SVG files, parse
and normalize XML, and return the complete SVG strings. Every returned preview
was checked for path content, expected count, and absence of `<text>` output.
This excludes host SVG ingestion, native resources, UI drawing, presentation,
and queuing. It is a reading pipeline measurement, not an isolated math engine
comparison.

Each paragraph contains Chinese body text and one fixed formula; the complex
formula includes a fraction, radical, sum and 2×2 matrix. Width is 520pt. Input
JSON, hashes, every timing/allocation sample, and final SVG files are under
`artifacts/baseline/`. All six first calls were retained separately; subsequent
seven repetitions per scenario comprise the table below. Since a new Typst
process starts on every service call, warm means repeated harness/filesystem
work rather than a persistent Typst engine. The first call does not prove cold
OS caches or fresh-harness initialization.

## Actual results

| Scenario | Median ms | Observed P95 ms | Median managed bytes |
| --- | ---: | ---: | ---: |
| Short, 1 paragraph | 172.047 | 197.768 | 450,736 |
| Short, 10 paragraphs | 184.570 | 207.788 | 3,530,608 |
| Short, 100 paragraphs | 406.356 | 415.529 | 34,725,792 |
| Complex, 1 paragraph | 160.952 | 171.876 | 582,312 |
| Complex, 10 paragraphs | 191.180 | 200.199 | 4,988,808 |
| Complex, 100 paragraphs | 442.520 | 478.592 | 50,778,336 |

P95 uses nearest rank with seven samples and therefore equals the maximum.
This is descriptive evidence with substantial sample and environmental limits.
The short/complex one-paragraph ordering is noise-sensitive and does not imply
complex math is faster. The committed summary and raw report checksum are in
`docs/reference/typst-reading-baseline-2026-10-19.json`.

Managed allocation is the true `GC.GetTotalAllocatedBytes(precise: true)` delta
of the whole harness process across the awaited service call. It includes
service infrastructure and returned strings. Working/private-memory snapshots
are also in the raw report but only refer to the harness. Typst child heap,
peak native memory, system-font caches, and complete host resource consumption
were **not measured**. No allocation or native memory result is described as
zero, and no engine speedup multiple is derived from process startup costs.

The warmed exact KaTeX baseline is recorded separately in
`docs/verification/p0-reference.md`, since browser and Typst measurement
boundaries differ.

## Budget status and remaining work

The confirmed first-version contract remains whole uncached formula compilation
P95 ≤10ms after initialization. This development CPU cannot stand in for the
roughly decade-old target CPU. No old reference machine is available: old-CPU
latency, allocation and memory acceptance remain **unverified**. The later ≥10×
goal is not achieved by these measurements.

Retain the architecture's provisional configurable caps for initial P1:
32,768 UTF-16 input units; 65,536 expanded tokens and semantic nodes; nesting
128; 4,096 matrix cells; 1,000 macro expansions. The P0 seed/calibration cases
fit these caps, but extreme-input recovery and a complete language corpus have
not calibrated them. Native exporter pixel limits, pooled workspace retention,
cache bytes, and per-stage parse/layout/output timing budgets also remain to
be measured with the real P1 engine. The existing reading pipeline's 34–51MB
100-paragraph managed allocation is a comparison baseline, not a justified
memory allowance for the redesigned engine.

Next verification: add P1 per-stage timing and actual result/workspace memory
measurements with caches disabled, then compare under the same workload and
scope. Keep first initialization, native prepare/submit/render, exports, and
UI work separately measurable; do not move work outside the ≤10ms boundary to
pass it.
