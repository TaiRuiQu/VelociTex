[CmdletBinding()]
param([switch]$SkipBuild, [string]$PackagePath = 'npm/velocitex')
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    if (!$SkipBuild) { & (Join-Path $PSScriptRoot 'build-wasm.ps1') -Locked }
    & dotnet run --project tests/VelociTex.Web.Tests -c Release -p:RestoreLockedMode=true -- artifacts/wasm/parity.json
    if ($LASTEXITCODE -ne 0) { throw 'Portable SVG assertions failed.' }
    $resolvedPackage = (Resolve-Path -LiteralPath $PackagePath).Path
    & npm pack $resolvedPackage --pack-destination artifacts/wasm --silent
    if ($LASTEXITCODE -ne 0) { throw 'npm pack failed.' }
    $metadata = Get-Content (Join-Path $resolvedPackage 'package.json') -Raw | ConvertFrom-Json
    $tarballPath = (Resolve-Path "artifacts/wasm/velocitex-$($metadata.version).tgz").Path
    $consumerPath = Join-Path $PSScriptRoot ('artifacts/wasm/consumers/' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force $consumerPath | Out-Null
    & npm install --prefix $consumerPath $tarballPath --ignore-scripts --no-audit --no-fund
    if ($LASTEXITCODE -ne 0) { throw 'Isolated npm consumer install failed.' }
    $installedPath = Join-Path $consumerPath 'node_modules/velocitex'
    $staticPath = Join-Path $consumerPath 'public/velocitex'
    & node (Join-Path $installedPath 'scripts/copy-assets.mjs') $staticPath
    if ($LASTEXITCODE -ne 0) { throw 'Runtime asset copy failed.' }
    Copy-Item tests/wasm/consumer.mts (Join-Path $consumerPath 'consumer.mts')
    & node tools/reference/node_modules/typescript/bin/tsc --noEmit --strict --module nodenext --target es2022 --lib es2022,dom (Join-Path $consumerPath 'consumer.mts')
    if ($LASTEXITCODE -ne 0) { throw 'TypeScript consumer failed.' }
    & node tools/reference/wasm-browser.mjs $installedPath $staticPath
    if ($LASTEXITCODE -ne 0) { throw 'Installed npm package browser verification failed.' }
    Write-Host "WASM/npm verification passed. Isolated consumer: $consumerPath"
}
finally { Pop-Location }
