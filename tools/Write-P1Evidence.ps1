param([switch]$Check)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
Push-Location $taskRoot
try {
    $sourceFiles = [ordered]@{}
    $rawFonts = [ordered]@{}
    $paths = @(& git ls-files --cached --others --exclude-standard) | Sort-Object -Unique
    if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate phase source files.' }
    foreach ($relative in $paths) {
        if ($relative -notmatch '^(src/|samples/|tests/|tools/|compatibility/|Directory\.|global\.json$|NuGet\.Config$|VelociTex\.slnx$|build\.ps1$|verify.*\.ps1$)') { continue }
        if ($relative -notmatch '\.(cs|csproj|json|mjs|py|ps1|props|slnx|Config|ttf|txt)$') { continue }
        if (!(Test-Path -LiteralPath $relative -PathType Leaf)) { continue }
        $bytes = [IO.File]::ReadAllBytes((Join-Path $taskRoot $relative))
        if ($relative.EndsWith('.ttf')) {
            $rawFonts[$relative] = [Convert]::ToHexStringLower([Security.Cryptography.SHA256]::HashData($bytes))
        } else {
            $canonical = [Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xfeff).Replace("`r`n", "`n")
            $sourceFiles[$relative] = [Convert]::ToHexStringLower([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($canonical)))
        }
    }
    $outputs = [ordered]@{}
    $artifactRoots = @('artifacts/p1/jit','artifacts/p1/aot','artifacts/p1/reference','artifacts/p1/winui/aot','artifacts/p1/review/jit','artifacts/p1/p0-regression/jit','artifacts/p1/p0-regression/aot','artifacts/p1/review/before-radical-fix')
    foreach ($directory in $artifactRoots) {
        if (!(Test-Path -LiteralPath $directory)) { throw "Missing evidence directory: $directory" }
        foreach ($file in Get-ChildItem -LiteralPath $directory -Recurse -File | Sort-Object FullName) {
            $relative = [IO.Path]::GetRelativePath($taskRoot,$file.FullName).Replace('\','/')
            if ($relative -match '/pair-\d{3}-') { continue } # Contact sheets and audit retain their source hashes.
            $outputs[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    foreach ($relative in @('artifacts/p1/runtime-parity.json','artifacts/p1/publish/VelociTex.P1.Tests.exe','artifacts/winui/publish/VelociTex.WinUI.Sample.exe')) {
        $outputs[$relative] = (Get-FileHash -LiteralPath $relative -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $jit = Get-Content artifacts/p1/jit/report.json -Raw | ConvertFrom-Json
    $aot = Get-Content artifacts/p1/aot/report.json -Raw | ConvertFrom-Json
    $ui = Get-Content artifacts/p1/winui/aot/winui-report.json -Raw | ConvertFrom-Json
    if ($jit.nativeAot -or !$aot.nativeAot -or !$ui.nativeAot) { throw 'P1 requires real JIT/AOT engine and AOT WinUI processes.' }
    if ($ui.executableSha256 -ne $outputs['artifacts/winui/publish/VelociTex.WinUI.Sample.exe']) { throw 'UI screenshot report executable differs from the recorded binary.' }
    $record = [ordered]@{
        schemaVersion = 1
        recordedDate = '2026-10-19'
        phase = 'P1'
        sourceHashPolicy = 'UTF-8 without BOM, LF canonical text bytes matching Git .gitattributes; fonts and output artifacts hash raw bytes. Source snapshot excludes this evidence file and mutable build products.'
        sourceFiles = $sourceFiles
        rawFontFiles = $rawFonts
        outputFiles = $outputs
        actualProcesses = [ordered]@{ jit = $jit.processId; nativeAot = $aot.processId; winuiNativeAot = $ui.processId }
        validFixtures = $aot.validFixtures
        invalidFixtures = $aot.invalidFixtures
        additionalCoreEdgeAssertions = $aot.additionalCoreEdgeAssertions
        nativeResourceBoundary = 'Managed complete-compile allocation and 100 renderer create/dispose process-private-byte/handle snapshots are recorded. Exact COM/native/GPU peak bytes remain unverified.'
    }
    $file = 'docs/reference/p1.evidence.json'
    $json = ($record | ConvertTo-Json -Depth 12) + "`n"
    if ($Check) {
        $expected = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json -AsHashtable
        foreach ($group in @('sourceFiles','rawFontFiles','outputFiles')) {
            if ($expected[$group].Count -ne $record[$group].Count) { throw "Evidence count differs: $group" }
            foreach ($key in $record[$group].Keys) { if ($expected[$group][$key] -ne $record[$group][$key]) { throw "Evidence differs: $key" } }
        }
        Write-Host "PASS P1 evidence: $($sourceFiles.Count) text files, $($rawFonts.Count) fonts, $($outputs.Count) outputs."
    } else {
        [IO.File]::WriteAllText((Join-Path $taskRoot $file),$json,[Text.UTF8Encoding]::new($false))
        Write-Host "Recorded P1 evidence: $($sourceFiles.Count) text files, $($rawFonts.Count) fonts, $($outputs.Count) outputs."
    }
} finally { Pop-Location }
