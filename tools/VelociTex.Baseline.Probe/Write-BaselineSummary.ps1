param([string] $RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path)
$ErrorActionPreference = 'Stop'
$reportPath = Join-Path $RepositoryRoot 'artifacts/baseline/typst-reading-baseline.json'
$report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json -AsHashtable -DateKind String
$summary = [ordered]@{
    schemaVersion = 1
    timestampUtc = $report.TimestampUtc
    framework = $report.Framework
    typstVersion = $report.TypstVersion
    typstSha256 = $report.TypstSha256
    downstreamAssemblySha256 = $report.DocumentsAssemblySha256
    repetitionsPerScenario = $report.Repetitions
    measurementBoundary = $report.MeasurementBoundary
    inputs = @($report.Inputs | ForEach-Object { @{ name = $_.Name; paragraphCount = $_.ParagraphCount; formulasPerParagraph = 1; widthPoints = $_.WidthPoints; sha256 = $_.Sha256 } })
    summaries = $report.Summaries
    rawEvidence = @{ path = 'artifacts/baseline/typst-reading-baseline.json'; sha256 = (Get-FileHash -LiteralPath $reportPath -Algorithm SHA256).Hash.ToLowerInvariant() }
    hardware = 'docs/reference/development-hardware.json'
}
$json = $summary | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText((Join-Path $RepositoryRoot 'docs/reference/typst-reading-baseline-2026-10-19.json'), $json + "`n", [Text.UTF8Encoding]::new($false))
Write-Output $json
