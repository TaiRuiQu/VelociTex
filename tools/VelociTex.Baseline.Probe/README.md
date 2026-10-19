# Existing downstream reading baseline

This development harness calls the existing compiled
`OhMyStudy.Documents.TypstDocumentService.RenderReadingAsync` with fixed synthetic
paragraphs. It reads a current downstream DLL and the existing pinned Typst
0.14.2 executable; it does not build or modify the downstream repository. Its
DLL reference is a development measurement dependency, not a library runtime
dependency. It is intentionally excluded from the product solution.

The default DLL path is the existing `src/OhMyStudy.Documents/bin/x64/Release/net10.0/`
output. The older `probes/.../bin/Release/` DLL did not expose the reading API.
Override the source with `-p:DownstreamDocumentsAssembly=<absolute DLL path>` if
needed. The DLL and executable hashes are recorded by the run.

```powershell
dotnet run --project tools/VelociTex.Baseline.Probe/VelociTex.Baseline.Probe.csproj -c Release -- C:\Users\XiaoQ\Documents\VelociTex C:\Users\XiaoQ\Documents\oh-my-study C:\Users\XiaoQ\Documents\VelociTex\artifacts\baseline 7
pwsh -File tools/VelociTex.Baseline.Probe/Write-HardwareManifest.ps1
pwsh -File tools/VelociTex.Baseline.Probe/Write-BaselineSummary.ps1
```

The first execution inside the managed filesystem sandbox failed with Typst
`Access is denied (os error 5)`. The same compiled harness was then actually run
outside that sandbox after automatic approval review. System font access is
needed by the unmodified downstream service. This is a local execution
requirement, not a request to change permissions or downstream files.

Six scenarios cover 1/10/100 short or complex paragraphs at 520pt. Every
paragraph contains one formula plus Chinese body text. Input JSON and SHA-256
values are saved. Each scenario has a first call and seven measured repetitions
by default. All calls start a new Typst child because that is what the actual
service does. Repeating the harness does not keep a Typst engine alive.

Timing includes delimiter splitting, the existing converter, source writing,
Typst process launch/compile, SVG reading and XML normalization. It excludes the
host's SVG ingestion, rendering, queues, and presentation. Total managed bytes
are measured across all harness threads using `GC.GetTotalAllocatedBytes`; they
include result strings and asynchronous service work. Working set and private
memory belong to the harness process only. Typst child peak/native memory is
not measured. First-call samples do not prove cold OS caches or fresh-process
initialization costs, and seven-sample P95 is descriptive, not a latency gate.

The raw report and SVGs remain in `artifacts/baseline/`. This baseline cannot
establish a VelociTex engine speedup, a 10ms engine pass, or decade-old hardware
acceptance.
