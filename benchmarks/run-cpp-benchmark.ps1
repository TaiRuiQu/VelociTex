[CmdletBinding()]
param([string]$Output = 'benchmarks/results/2026-10-20-preview6')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $manifest = Get-Content "$Output/manifest.json" -Raw | ConvertFrom-Json
    $dll = (Resolve-Path artifacts/core-native/VelociTex.Core.Native.dll).Path
    $dllHash = (Get-FileHash -LiteralPath $dll).Hash.ToLowerInvariant()
    if ($dllHash -cne $manifest.fileSha256.'artifacts/core-native/VelociTex.Core.Native.dll') { throw 'The C++ DLL changed since the main campaign.' }
    $start = [DateTimeOffset]::UtcNow.ToString('O')
    for ($run = 1; $run -le $manifest.runs; $run++) {
        $directory = Join-Path $Output "run-$run/cpp"
        New-Item -ItemType Directory -Force $directory | Out-Null
        & ./artifacts/benchmarks-cpp/native-core.exe $dll "$directory/cpp-report.json" $manifest.samples $manifest.targetBatchMs
        if ($LASTEXITCODE) { throw 'Native C++ benchmark failed.' }
    }
    $hashes = [ordered]@{}
    foreach ($file in @('benchmarks/native-core.cpp', 'benchmarks/build-native-benchmark.ps1', 'benchmarks/run-cpp-benchmark.ps1', 'artifacts/benchmarks-cpp/native-core.exe', 'artifacts/benchmarks-cpp/native-workloads.inc')) {
        $hashes[$file] = (Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant()
    }
    [ordered]@{
        schemaVersion = 2; startedUtc = $start; endedUtc = [DateTimeOffset]::UtcNow.ToString('O')
        note = 'Supplemental pure C++ public-ABI campaign, run sequentially after the four-host rotated campaign; same frozen corpus and native DLL.'
        compiler = 'MSVC /O2 /DNDEBUG /MT /std:c++20'; nativeDllSha256 = $dllHash; fileSha256 = $hashes
    } | ConvertTo-Json -Depth 6 | Set-Content "$Output/cpp-manifest.json" -Encoding utf8NoBOM
}
finally { Pop-Location }
