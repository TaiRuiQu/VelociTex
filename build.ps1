[CmdletBinding()]
param(
    [ValidateSet('Build', 'Test', 'Aot', 'All')][string]$Target = 'Build',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$Locked
)
$ErrorActionPreference = 'Stop'
$env:DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE = 'true'
$env:MSBuildEnableWorkloadResolver = 'false'
Push-Location $PSScriptRoot
try {
    # Build the C++ ABI first so every managed output and package contains the
    # exact native implementation used by the interop layer.
    & ./native/build-core.ps1 -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { throw 'Native Core build failed.' }
    $restore = @()
    if ($Locked) { $restore += '-p:RestoreLockedMode=true' }
    function Invoke-Dotnet([string[]]$Arguments) {
        & dotnet @Arguments
        if ($LASTEXITCODE -ne 0) { throw "dotnet failed: $($Arguments -join ' ')" }
    }
    Invoke-Dotnet (@('build', 'VelociTex.slnx', '-c', $Configuration, '-p:Platform=x64', '-p:PublishAot=false', '-m:1', '--nologo') + $restore)
    if ($Target -in @('Test', 'All')) {
        Invoke-Dotnet (@('build', 'tests/VelociTex.Core.Tests', '-c', $Configuration, '-p:Platform=x64', '-p:PublishAot=false', '-m:1', '--nologo') + $restore)
        Invoke-Dotnet @('run', '--project', 'tests/VelociTex.Core.Tests', '-c', $Configuration, '-p:Platform=x64', '-p:PublishAot=false', '--no-build', '--', 'artifacts/tests/jit')
    }
    if ($Target -in @('Aot', 'All')) {
        Invoke-Dotnet (@('publish', 'tests/VelociTex.Core.Tests', '-c', $Configuration, '-r', 'win-x64', '-p:PublishAot=true', '-p:Platform=x64', '-o', 'artifacts/tests/publish', '-m:1', '--nologo') + $restore)
        & ./artifacts/tests/publish/VelociTex.Core.Tests.exe artifacts/tests/aot
        if ($LASTEXITCODE -ne 0) { throw 'Native AOT regression tests failed.' }
    }
    if ($Target -eq 'All') {
        & node tools/runtime-parity.mjs
        if ($LASTEXITCODE -ne 0) { throw 'JIT/AOT output parity failed.' }
    }
}
finally { Pop-Location }
