param(
    [ValidateSet('Build', 'Native', 'WinUI', 'P1', 'All')][string]$Target = 'Build',
    [switch]$Locked
)
$ErrorActionPreference = 'Stop'
$env:DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE = 'true'
$env:MSBuildEnableWorkloadResolver = 'false'
Push-Location $PSScriptRoot
try {
    $restore = @()
    if ($Locked) { $restore += '-p:RestoreLockedMode=true' }
    function Invoke-Dotnet([string[]]$Arguments) {
        & dotnet @Arguments
        if ($LASTEXITCODE -ne 0) { throw "dotnet failed: $($Arguments -join ' ')" }
    }
    Invoke-Dotnet (@('build', 'VelociTex.slnx', '-c', 'Debug', '-p:Platform=x64', '--nologo') + $restore)
    if ($Target -in @('Native', 'All')) {
        Invoke-Dotnet (@('build', 'tools/VelociTex.Native.Probe/VelociTex.Native.Probe.csproj', '-c', 'Release', '-p:PublishAot=false', '-p:Platform=x64', '-t:Rebuild', '--nologo') + $restore)
        Invoke-Dotnet @('tools/VelociTex.Native.Probe/bin/x64/Release/net10.0-windows10.0.26100.0/win-x64/VelociTex.Native.Probe.dll', 'artifacts/native/jit')
        Invoke-Dotnet (@('publish', 'tools/VelociTex.Native.Probe/VelociTex.Native.Probe.csproj', '-c', 'Release', '-r', 'win-x64', '-p:PublishAot=true', '-p:Platform=x64', '-o', 'artifacts/native/publish', '--nologo') + $restore)
        & ./artifacts/native/publish/VelociTex.Native.Probe.exe artifacts/native/aot
        if ($LASTEXITCODE -ne 0) { throw 'Native AOT probe failed.' }
    }
    if ($Target -in @('P1', 'All')) {
        Invoke-Dotnet (@('build', 'tests/VelociTex.P1.Tests/VelociTex.P1.Tests.csproj', '-c', 'Release', '-p:PublishAot=false', '-p:Platform=x64', '-t:Rebuild', '--nologo') + $restore)
        Invoke-Dotnet @('tests/VelociTex.P1.Tests/bin/x64/Release/net10.0-windows10.0.26100.0/win-x64/VelociTex.P1.Tests.dll', 'artifacts/p1/jit')
        Invoke-Dotnet (@('publish', 'tests/VelociTex.P1.Tests/VelociTex.P1.Tests.csproj', '-c', 'Release', '-r', 'win-x64', '-p:PublishAot=true', '-p:Platform=x64', '-o', 'artifacts/p1/publish', '--nologo') + $restore)
        & ./artifacts/p1/publish/VelociTex.P1.Tests.exe artifacts/p1/aot
        if ($LASTEXITCODE -ne 0) { throw 'P1 AOT engine assertions failed.' }
    }
    if ($Target -in @('WinUI', 'P1', 'All')) {
        Invoke-Dotnet (@('publish', 'samples/VelociTex.WinUI.Sample/VelociTex.WinUI.Sample.csproj', '-c', 'Release', '-p:Platform=x64', '-o', 'artifacts/winui/publish', '--nologo') + $restore)
        $smokeOutput = if ($Target -eq 'WinUI') { 'artifacts/winui/aot' } else { 'artifacts/p1/winui/aot' }
        [string[]]$smokeArguments = if ($Target -eq 'WinUI') { @('--p0', '--smoke') } else { @('--p1-smoke') }
        $smokeArguments += ('"{0}/{1}"' -f $PSScriptRoot,$smokeOutput)
        $reportPath = Join-Path $smokeOutput 'winui-report.json'
        if (Test-Path -LiteralPath $reportPath) { Remove-Item -LiteralPath $reportPath }
        $application = Start-Process -FilePath ./artifacts/winui/publish/VelociTex.WinUI.Sample.exe -ArgumentList $smokeArguments -WindowStyle Hidden -PassThru
        if (!$application.WaitForExit(60000)) {
            $application.Kill()
            throw 'The owned WinUI smoke process did not finish within 60 seconds.'
        }
        if ($application.ExitCode -ne 0) { throw "WinUI exited with $($application.ExitCode)." }
        $errorPath = Join-Path $smokeOutput 'smoke-error.txt'
        if (Test-Path -LiteralPath $errorPath) { throw (Get-Content -LiteralPath $errorPath -Raw) }
        $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
        if (!$report.nativeAot -or $report.processId -ne $application.Id) { throw 'WinUI smoke did not complete in the expected Native AOT process.' }
        Write-Host "WinUI AOT completed. Inspect $smokeOutput screenshots for visual acceptance."
    }
}
finally { Pop-Location }
