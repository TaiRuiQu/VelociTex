[CmdletBinding()]
param(
    [string]$Output = 'benchmarks/results/2026-10-20-preview6',
    [int]$Runs = 3,
    [int]$Samples = 15,
    [int]$BatchMs = 25,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $env:MSBuildEnableWorkloadResolver = 'false'
    $env:DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE = 'true'
    $env:VELOCITEX_NATIVE_DIR = Join-Path $root 'artifacts/core-native'
    if (!$SkipBuild) {
        & ./native/build-core.ps1
        if ($LASTEXITCODE) { throw 'Native build failed.' }
        foreach ($project in @('VelociTex.Benchmarks', 'VelociTex.WinUI.Bench')) {
            dotnet build "benchmarks/$project" -c Release -p:Platform=x64 -m:1 --nologo
            if ($LASTEXITCODE) { throw "Build failed: $project" }
        }
        & ./build-wasm.ps1
        if ($LASTEXITCODE) { throw 'WASM AOT build failed.' }
    }
    $native = (Resolve-Path 'benchmarks/VelociTex.Benchmarks/bin/x64/Release/net10.0-windows10.0.26100.0/win-x64/VelociTex.Benchmarks.exe').Path
    $winui = (Resolve-Path 'benchmarks/VelociTex.WinUI.Bench/bin/x64/Release/net10.0-windows10.0.26100.0/win-x64/VelociTex.WinUI.Bench.exe').Path
    New-Item -ItemType Directory -Force $Output | Out-Null
    if (Test-Path "$Output/manifest.json") { throw 'Choose a fresh output directory to preserve previous measurements.' }
    $inputs = @('benchmarks/workloads.json', 'benchmarks/BatchTiming.cs', 'benchmarks/browser/cpu-runner.js', 'benchmarks/browser/page.html', 'benchmarks/browser/bench.mjs', 'benchmarks/run-benchmarks.ps1', 'benchmarks/VelociTex.Benchmarks/CpuBenchmark.cs', 'benchmarks/VelociTex.WinUI.Bench/BenchWindow.cs', 'artifacts/core-native/VelociTex.Core.Native.dll', $native, $winui)
    $inputs += @(Get-ChildItem npm/velocitex/dist -Recurse -File | ForEach-Object { [IO.Path]::GetRelativePath($root, $_.FullName) })
    $hashes = [ordered]@{}
    foreach ($file in $inputs) {
        $key = if ([IO.Path]::IsPathRooted($file)) { [IO.Path]::GetRelativePath($root, $file) } else { $file }
        $hashes[$key.Replace('\', '/')] = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $manifest = [ordered]@{
        schemaVersion = 2; utc = [DateTimeOffset]::UtcNow.ToString('O'); sourceRevision = (& git rev-parse HEAD).Trim()
        sourceHasChanges = [bool](& git status --porcelain); runs = $Runs; samples = $Samples; targetBatchMs = $BatchMs
        cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name)
        memoryBytes = (Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory
        fileSha256 = $hashes; order = @()
    }
    $engines = @('native', 'velocitex', 'katex', 'winui')
    for ($run = 1; $run -le $Runs; $run++) {
        for ($index = 0; $index -lt $engines.Count; $index++) {
            $engine = $engines[($index + $run - 1) % $engines.Count]
            $directory = Join-Path $Output "run-$run/$engine"
            New-Item -ItemType Directory -Force $directory | Out-Null
            $manifest.order += "run-$run/$engine"
            Write-Host "Running $engine replicate $run/$Runs"
            if ($engine -in @('velocitex', 'katex')) {
                node benchmarks/browser/bench.mjs --engine $engine --samples $Samples --batch-ms $BatchMs --out $directory
                if ($LASTEXITCODE) { throw "Browser failed: $engine" }
            } elseif ($engine -eq 'native') {
                & $native --out $directory --samples $Samples --batch-ms $BatchMs
                if ($LASTEXITCODE) { throw 'Native benchmark failed.' }
            } else {
                $absoluteOutput = [IO.Path]::GetFullPath($directory)
                $process = Start-Process -FilePath $winui -ArgumentList @('--out', "`"$absoluteOutput`"", '--samples', $Samples, '--batch-ms', $BatchMs) -WindowStyle Hidden -PassThru
                if (!$process.WaitForExit(180000)) { $process.Kill(); throw 'WinUI benchmark timed out.' }
                if ($process.ExitCode -ne 0 -or !(Test-Path "$directory/winui-report.json")) { throw 'WinUI benchmark failed; inspect its error report.' }
            }
        }
    }
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$Output/manifest.json" -Encoding utf8NoBOM
}
finally { Pop-Location }
