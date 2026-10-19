param(
    [ValidateSet('Build', 'Native', 'WinUI', 'All')][string]$Target = 'Build',
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
    if ($Target -in @('WinUI', 'All')) {
        Invoke-Dotnet (@('publish', 'samples/VelociTex.WinUI.Sample/VelociTex.WinUI.Sample.csproj', '-c', 'Release', '-p:Platform=x64', '-o', 'artifacts/winui/publish', '--nologo') + $restore)
        if (Test-Path -LiteralPath artifacts/winui/aot/winui-report.json) { Remove-Item -LiteralPath artifacts/winui/aot/winui-report.json }
        $application = Start-Process -FilePath ./artifacts/winui/publish/VelociTex.WinUI.Sample.exe -ArgumentList '--smoke',('"{0}/artifacts/winui/aot"' -f $PSScriptRoot) -WindowStyle Hidden -PassThru
        $application.WaitForExit()
        if ($application.ExitCode -ne 0) { throw "WinUI exited with $($application.ExitCode)." }
        if (Test-Path -LiteralPath artifacts/winui/aot/smoke-error.txt) { throw (Get-Content -LiteralPath artifacts/winui/aot/smoke-error.txt -Raw) }
        $report = Get-Content -LiteralPath artifacts/winui/aot/winui-report.json -Raw | ConvertFrom-Json
        if (!$report.nativeAot -or $report.processId -ne $application.Id) { throw 'WinUI smoke did not complete in the expected Native AOT process.' }
        Write-Host 'WinUI AOT completed. Inspect artifacts/winui/aot screenshots for visual acceptance.'
    }
}
finally { Pop-Location }
