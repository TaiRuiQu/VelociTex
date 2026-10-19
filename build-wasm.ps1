[CmdletBinding()]
param([switch]$NoAot, [switch]$Locked, [string]$PackageVersion)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $publishPath = Join-Path $PSScriptRoot 'artifacts/wasm/publish'
    $packagePath = Join-Path $PSScriptRoot 'npm/velocitex'
    $distPath = Join-Path $packagePath 'dist'
    if ($PackageVersion) {
        & node --input-type=module -e 'import {releaseVersion} from "./npm/velocitex/scripts/release.mjs"; releaseVersion("v" + process.argv[1]);' $PackageVersion
        if ($LASTEXITCODE -ne 0) { throw 'Invalid npm release version.' }
    }
    # Remove stale hashed assets from previous publish modes before packaging.
    if (Test-Path -LiteralPath $publishPath) {
        $resolvedPublish = (Resolve-Path -LiteralPath $publishPath).Path
        if ($resolvedPublish -ne (Join-Path $PSScriptRoot 'artifacts/wasm/publish')) { throw 'Unexpected WASM publish path.' }
        Remove-Item -LiteralPath $resolvedPublish -Recurse -Force
    }
    $arguments = @('publish', 'src/VelociTex.Wasm/VelociTex.Wasm.csproj', '-c', 'Release', '-o', $publishPath, '--nologo', "-p:RunAOTCompilation=$((!$NoAot).ToString().ToLowerInvariant())")
    if ($Locked) { $arguments += '-p:RestoreLockedMode=true' }
    & dotnet @arguments
    if ($LASTEXITCODE -ne 0) { throw 'WASM publish failed.' }
    # Delete only this package's resolved generated dist, never caller-supplied paths.
    if (Test-Path -LiteralPath $distPath) {
        $resolvedDist = (Resolve-Path -LiteralPath $distPath).Path
        if ($resolvedDist -ne (Join-Path $PSScriptRoot 'npm/velocitex/dist')) { throw 'Unexpected distribution path.' }
        Remove-Item -LiteralPath $resolvedDist -Recurse -Force
    }
    New-Item -ItemType Directory -Force $distPath | Out-Null
    $frameworkPath = Join-Path $publishPath 'wwwroot/_framework'
    if (!(Test-Path -LiteralPath (Join-Path $frameworkPath 'dotnet.js'))) { throw 'WASM SDK publish output is missing dotnet.js.' }
    Copy-Item -LiteralPath $frameworkPath -Destination (Join-Path $distPath 'runtime') -Recurse
    Copy-Item -Path (Join-Path $packagePath 'src/*') -Destination $distPath
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'LICENSE') -Destination $packagePath
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'THIRD-PARTY-NOTICES.md') -Destination $packagePath
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'licenses') -Destination $packagePath -Recurse -Force
    $metadata = Get-Content (Join-Path $packagePath 'package.json') -Raw | ConvertFrom-Json
    $info = [ordered]@{ configuration = 'Release'; aot = !$NoAot; sdk = (& dotnet --version); katex = '0.19.0'; sourceRevision = (& git rev-parse HEAD); sourceHasChanges = [bool](& git status --porcelain); packageVersion = $metadata.version; versionSource = 'package.json' }
    $json = $info | ConvertTo-Json
    [IO.File]::WriteAllText((Join-Path $distPath 'build-info.json'), $json, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "WASM npm assets built at $distPath"
    if ($PackageVersion) {
        $stagedPath = Join-Path $PSScriptRoot 'artifacts/wasm/npm-package'
        if (Test-Path -LiteralPath $stagedPath) {
            $resolvedStage = (Resolve-Path -LiteralPath $stagedPath).Path
            if ($resolvedStage -ne (Join-Path $PSScriptRoot 'artifacts/wasm/npm-package')) { throw 'Unexpected staged package path.' }
            Remove-Item -LiteralPath $resolvedStage -Recurse -Force
        }
        & node npm/velocitex/scripts/release.mjs stage $stagedPath $PackageVersion
        if ($LASTEXITCODE -ne 0) { throw 'Unable to stage the versioned npm package.' }
        Write-Host "Versioned npm package staged at $stagedPath"
    }
}
finally { Pop-Location }
