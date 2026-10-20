param(
    [string]$Feed = 'artifacts/packages/release',
    [string]$Output = 'artifacts/tests/package-consumer',
    [ValidatePattern('^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$')][string]$Version = '1.0.0-preview4',
    [switch]$UpdateLocks,
    # Store locks for the exact package version being tested.
    [string]$LockDirectory
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Resolve-RepoPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $repo $Path))
}
$feedPath = Resolve-RepoPath $Feed
$outputPath = Resolve-RepoPath $Output
$templatePath = Join-Path $repo 'tests/package-consumer'
$lockTemplatePath = if ($LockDirectory) { Resolve-RepoPath $LockDirectory } else { Join-Path $outputPath 'locks' }
$packages = @('VelociTex.Core', 'VelociTex.Rendering.Direct2D')
foreach ($id in $packages) {
    if (!(Test-Path -LiteralPath (Join-Path $feedPath "$id.$Version.nupkg"))) { throw "Missing local package: $id.$Version.nupkg in $feedPath" }
}
$runPath = Join-Path $outputPath ('run-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$appPath = Join-Path $runPath 'app'
$isolatedPackages = Join-Path $runPath 'packages'
New-Item -ItemType Directory -Path $appPath, $isolatedPackages -Force | Out-Null
if ($UpdateLocks) { New-Item -ItemType Directory -Path $lockTemplatePath -Force | Out-Null }
$utf8 = [Text.UTF8Encoding]::new($false)
function Write-Text([string]$Path, [string]$Text) { [IO.File]::WriteAllText($Path, $Text, $utf8) }
$programTemplate = Join-Path $templatePath 'Program.cs'
Copy-Item -LiteralPath $programTemplate -Destination (Join-Path $appPath 'Program.cs')
Copy-Item -LiteralPath (Join-Path $repo 'tools/reference/data/katex-0.19.0.lock.json') -Destination (Join-Path $appPath 'font-lock.json')
Copy-Item -LiteralPath (Join-Path $repo 'global.json') -Destination (Join-Path $appPath 'global.json')
Write-Text (Join-Path $appPath 'Directory.Build.props') @'
<Project>
  <PropertyGroup>
    <LangVersion>14</LangVersion><Nullable>enable</Nullable><ImplicitUsings>enable</ImplicitUsings>
    <EnableWindowsTargeting>true</EnableWindowsTargeting><IsAotCompatible>true</IsAotCompatible>
    <JsonSerializerIsReflectionEnabledByDefault>false</JsonSerializerIsReflectionEnabledByDefault>
    <RestorePackagesWithLockFile>true</RestorePackagesWithLockFile><Deterministic>true</Deterministic>
    <NuGetAudit>true</NuGetAudit><WarningsAsErrors>$(WarningsAsErrors);IL2026;IL2057;IL2060;IL2070;IL2072;IL2075;IL2091;IL2104;IL3050;IL3053</WarningsAsErrors>
  </PropertyGroup>
</Project>
'@
Write-Text (Join-Path $appPath 'Directory.Build.targets') '<Project />'
Write-Text (Join-Path $appPath 'Directory.Packages.props') @"
<Project>
  <PropertyGroup><ManagePackageVersionsCentrally>true</ManagePackageVersionsCentrally></PropertyGroup>
  <ItemGroup><PackageVersion Include="VelociTex.Core" Version="$Version" /><PackageVersion Include="VelociTex.Rendering.Direct2D" Version="$Version" /></ItemGroup>
</Project>
"@
$escapedFeed = [Security.SecurityElement]::Escape($feedPath)
$escapedPackages = [Security.SecurityElement]::Escape($isolatedPackages)
Write-Text (Join-Path $appPath 'NuGet.Config') @"
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources><clear /><add key="local-preview" value="$escapedFeed" /><add key="nuget.org" value="https://api.nuget.org/v3/index.json" protocolVersion="3" /></packageSources>
  <packageSourceMapping><clear /><packageSource key="local-preview"><package pattern="VelociTex.*" /></packageSource><packageSource key="nuget.org"><package pattern="*" /></packageSource></packageSourceMapping>
  <config><add key="globalPackagesFolder" value="$escapedPackages" /></config>
</configuration>
"@
Write-Text (Join-Path $appPath 'VelociTex.PackageConsumer.csproj') @'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType><TargetFramework>net10.0-windows10.0.26100.0</TargetFramework>
    <WindowsSdkPackageVersion>10.0.26100.87</WindowsSdkPackageVersion><SupportedOSPlatformVersion>10.0.19041.0</SupportedOSPlatformVersion>
    <RuntimeIdentifier>win-x64</RuntimeIdentifier><IsPackable>false</IsPackable>
    <SelfContained Condition="'$(PublishAot)' == 'true'">true</SelfContained><SelfContained Condition="'$(PublishAot)' != 'true'">false</SelfContained>
    <NuGetLockFilePath Condition="'$(PublishAot)' == 'true'">packages.aot.lock.json</NuGetLockFilePath>
    <NuGetLockFilePath Condition="'$(PublishAot)' != 'true'">packages.jit.lock.json</NuGetLockFilePath>
  </PropertyGroup>
  <ItemGroup><PackageReference Include="VelociTex.Core" /><PackageReference Include="VelociTex.Rendering.Direct2D" /><None Update="font-lock.json" CopyToOutputDirectory="PreserveNewest" /></ItemGroup>
</Project>
'@
$project = Join-Path $appPath 'VelociTex.PackageConsumer.csproj'
$config = Join-Path $appPath 'NuGet.Config'
[xml]$projectXml = Get-Content -LiteralPath $project -Raw
if ($projectXml.SelectNodes('//ProjectReference').Count -ne 0 -or $projectXml.SelectNodes('//PackageReference').Count -ne 2) { throw 'The clean consumer must use exactly two public PackageReferences.' }
$logs = [Collections.Generic.List[string]]::new()
function Invoke-Dotnet([string]$Name, [string[]]$Arguments) {
    $log = Join-Path $runPath ($Name + '.log')
    & dotnet @Arguments *> $log
    $result = $LASTEXITCODE
    $logs.Add($log)
    if ($result -ne 0) { Get-Content -LiteralPath $log -Tail 60; throw "dotnet failed ($result): $($Arguments -join ' ')" }
}
function Restore-Consumer([string]$Mode, [bool]$Aot) {
    $lockName = if ($Aot) { 'packages.aot.lock.json' } else { 'packages.jit.lock.json' }
    $lockPath = Join-Path $appPath $lockName
    $templateLock = Join-Path $lockTemplatePath $lockName
    if (!$UpdateLocks) {
        if (!(Test-Path -LiteralPath $templateLock)) { throw "Missing reviewed consumer lock $templateLock; first generate it with -UpdateLocks, then run the default locked verification." }
        Copy-Item -LiteralPath $templateLock -Destination $lockPath
    }
    $arguments = @('restore', $project, '--configfile', $config, '--packages', $isolatedPackages, '-r', 'win-x64', "-p:PublishAot=$($Aot.ToString().ToLowerInvariant())", '-p:Platform=x64', '--nologo')
    if ($UpdateLocks) {
        Invoke-Dotnet ($Mode + '-restore-create-lock') ($arguments + @('--force-evaluate'))
        Copy-Item -LiteralPath $lockPath -Destination $templateLock
    }
    Invoke-Dotnet ($Mode + '-restore-locked') ($arguments + @('--locked-mode'))
}
function Read-Assets([string]$Mode) {
    $path = Join-Path $appPath 'obj/project.assets.json'
    $snapshot = Join-Path $runPath ($Mode + '-project.assets.json')
    Copy-Item -LiteralPath $path -Destination $snapshot
    $assets = Get-Content -LiteralPath $snapshot -Raw | ConvertFrom-Json
    foreach ($library in $assets.libraries.PSObject.Properties) {
        if ($library.Value.type -ne 'package') { throw "The clean consumer resolved a non-package library: $($library.Name)" }
    }
    foreach ($folder in $assets.packageFolders.PSObject.Properties.Name) {
        if ([IO.Path]::GetFullPath($folder).TrimEnd('\', '/') -ne $isolatedPackages.TrimEnd('\', '/')) { throw "A package folder escaped isolation: $folder" }
    }
    foreach ($id in $packages) {
        if (!$assets.libraries.PSObject.Properties["$id/$Version"]) { throw "The consumer did not resolve exact $id/$Version." }
    }
    return $assets
}
function Run-Consumer([string]$Mode, [string]$Executable, [string[]]$Arguments, [bool]$ExpectedAot) {
    $stdout = Join-Path $runPath ($Mode + '-run.log')
    $stderr = Join-Path $runPath ($Mode + '-run-error.log')
    $process = Start-Process -FilePath $Executable -ArgumentList $Arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if (!$process.WaitForExit(60000)) { $process.Kill(); throw "Owned $Mode consumer did not finish within 60 seconds." }
    $process.Refresh()
    if ($process.ExitCode -ne 0) { Get-Content -LiteralPath $stdout; Get-Content -LiteralPath $stderr; throw "$Mode consumer failed with $($process.ExitCode)." }
    $reportPath = Join-Path (Join-Path $runPath $Mode) 'consumer-report.json'
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if ($report.ProcessId -ne $process.Id -or $report.NativeAot -ne $ExpectedAot -or $report.PackageVersion -ne $Version) { throw "$Mode report did not come from the expected runtime process and package version." }
    $logs.Add($stdout); $logs.Add($stderr)
    return $report
}
$priorWorkloadSetting = $env:DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE
$priorResolverSetting = $env:MSBuildEnableWorkloadResolver
$env:DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE = 'true'
$env:MSBuildEnableWorkloadResolver = 'false'
Push-Location $appPath
try {
    Restore-Consumer 'jit' $false
    $jitAssets = Read-Assets 'jit'
    $jitBin = Join-Path $runPath 'jit-bin'
    Invoke-Dotnet 'jit-build' @('build', $project, '-c', 'Release', '-r', 'win-x64', '-p:PublishAot=false', '-p:Platform=x64', '-p:RestoreLockedMode=true', '--no-restore', '-o', $jitBin, '--nologo')
    $jitReport = Run-Consumer 'jit' (Get-Command dotnet).Source @(('"' + (Join-Path $jitBin 'VelociTex.PackageConsumer.dll') + '"'), ('"' + (Join-Path $runPath 'jit') + '"'), '--expect-jit', "--package-version=$Version") $false
    Restore-Consumer 'aot' $true
    $aotAssets = Read-Assets 'aot'
    $publish = Join-Path $runPath 'publish'
    Invoke-Dotnet 'aot-publish' @('publish', $project, '-c', 'Release', '-r', 'win-x64', '-p:PublishAot=true', '-p:Platform=x64', '-p:RestoreLockedMode=true', '--no-restore', '-o', $publish, '--nologo')
    $aotExecutable = Join-Path $publish 'VelociTex.PackageConsumer.exe'
    $aotReport = Run-Consumer 'aot' $aotExecutable @(('"' + (Join-Path $runPath 'aot') + '"'), '--expect-aot', "--package-version=$Version") $true
}
finally { Pop-Location; $env:DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE = $priorWorkloadSetting; $env:MSBuildEnableWorkloadResolver = $priorResolverSetting }

function Hash-File([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Hash-PackageDll([string]$Path, [string]$File) {
    $zip = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $entry = @($zip.Entries | Where-Object { $_.FullName -match '^lib/[^/]+/' -and $_.Name -eq $File })
        if ($entry.Count -ne 1) { throw "Package $Path must contain exactly one compatible $File." }
        $stream = $entry[0].Open(); $hash = [Security.Cryptography.SHA256]::Create()
        try { $digest = [Convert]::ToHexString($hash.ComputeHash($stream)).ToLowerInvariant() }
        finally { $hash.Dispose(); $stream.Dispose() }
        return [ordered]@{ entry = $entry[0].FullName; sha256 = $digest; bytes = $entry[0].Length }
    }
    finally { $zip.Dispose() }
}
$dllChecks = @()
foreach ($id in $packages) {
    $nupkg = Join-Path $feedPath "$id.$Version.nupkg"
    $packageDll = Hash-PackageDll $nupkg ($id + '.dll')
    $restoredDll = Join-Path (Join-Path (Join-Path $isolatedPackages $id.ToLowerInvariant()) $Version) $packageDll.entry
    $runtimeDll = Join-Path $jitBin ($id + '.dll')
    if ((Hash-File $restoredDll) -ne $packageDll.sha256 -or (Hash-File $runtimeDll) -ne $packageDll.sha256) { throw "$id restored or JIT-loaded DLL differs from the actual nupkg entry." }
    $metadataPath = Join-Path (Join-Path (Join-Path $isolatedPackages $id.ToLowerInvariant()) $Version) '.nupkg.metadata'
    $metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
    if ($metadata.source.TrimEnd('\', '/') -ne $feedPath.TrimEnd('\', '/')) { throw "$id was not restored from the selected local feed: $($metadata.source)" }
    $dllChecks += [ordered]@{ id = $id; version = $Version; nupkg = $nupkg; packageSha256 = Hash-File $nupkg; entry = $packageDll.entry; dllSha256 = $packageDll.sha256; restoredDll = $restoredDll; jitRuntimeDll = $runtimeDll; equal = $true }
}
$fontLock = Get-Content -LiteralPath (Join-Path $appPath 'font-lock.json') -Raw | ConvertFrom-Json
foreach ($runtimeReport in @($jitReport, $aotReport)) {
    $minimumFormulas = 9
    if ($runtimeReport.Fonts.Count -ne 20 -or $runtimeReport.Formulas.Count -lt $minimumFormulas -or $runtimeReport.Checks.Count -lt 90) { throw 'Consumer did not execute the full public API assertions.' }
    foreach ($font in $fontLock.ttfFonts) {
        $actual = @($runtimeReport.Fonts | Where-Object File -eq $font.file)
        if ($actual.Count -ne 1 -or $actual[0].Sha256 -ne $font.sha256 -or $actual[0].Bytes -ne $font.bytes) { throw "Consumer font bytes differ from locked KaTeX: $($font.file)" }
    }
}
$parity = @()
foreach ($formula in $jitReport.Formulas) {
    $other = @($aotReport.Formulas | Where-Object Id -eq $formula.Id)
    if ($other.Count -ne 1 -or $formula.Width -ne $other[0].Width -or $formula.Height -ne $other[0].Height -or $formula.Baseline -ne $other[0].Baseline) { throw "JIT and AOT geometry differ: $($formula.Id)" }
    foreach ($export in $formula.Exports) {
        $aotExport = @($other[0].Exports | Where-Object File -eq $export.File)
        if ($aotExport.Count -ne 1 -or $export.Sha256 -ne $aotExport[0].Sha256) { throw "JIT and AOT export bytes differ: $($export.File)" }
        $jitExportPath = Join-Path (Join-Path $runPath 'jit') $export.File
        $aotExportPath = Join-Path (Join-Path $runPath 'aot') $export.File
        if ((Hash-File $jitExportPath) -ne $export.Sha256 -or (Hash-File $aotExportPath) -ne $export.Sha256) { throw "Actual export bytes differ from the runtime report: $($export.File)" }
        $parity += [ordered]@{ file = $export.File; sha256 = $export.Sha256; jitPath = $jitExportPath; aotPath = $aotExportPath; equal = $true }
    }
}
foreach ($name in @('error-fallback.png', 'error-fallback.svg', 'error-fallback.mathml')) {
        $jitPath = Join-Path (Join-Path $runPath 'jit') $name
        $aotPath = Join-Path (Join-Path $runPath 'aot') $name
        $digest = Hash-File $jitPath
        if ($digest -ne (Hash-File $aotPath)) { throw "JIT and AOT literal-source presentation bytes differ: $name" }
        $parity += [ordered]@{ file = $name; sha256 = $digest; jitPath = $jitPath; aotPath = $aotPath; equal = $true }
}
$sourceProgram = $programTemplate
if ((Hash-File $sourceProgram) -ne (Hash-File (Join-Path $appPath 'Program.cs'))) { throw 'The source template changed while its clean consumer was running.' }
foreach ($lockName in @('packages.jit.lock.json', 'packages.aot.lock.json')) {
    if ((Hash-File (Join-Path $lockTemplatePath $lockName)) -ne (Hash-File (Join-Path $appPath $lockName))) { throw "The consumer lock changed while running: $lockName" }
}
$evidencePaths = @($PSCommandPath, $sourceProgram, $project, (Join-Path $appPath 'Program.cs'), (Join-Path $appPath 'Directory.Build.props'), (Join-Path $appPath 'Directory.Build.targets'), (Join-Path $appPath 'Directory.Packages.props'), $config,
    (Join-Path $appPath 'global.json'), (Join-Path $appPath 'font-lock.json'), (Join-Path $appPath 'packages.jit.lock.json'), (Join-Path $appPath 'packages.aot.lock.json'),
    (Join-Path $runPath 'jit-project.assets.json'), (Join-Path $runPath 'aot-project.assets.json'), (Join-Path $runPath 'jit/consumer-report.json'), (Join-Path $runPath 'aot/consumer-report.json'), $aotExecutable) + $logs.ToArray()
$evidence = @($evidencePaths | ForEach-Object { [ordered]@{ path = $_; bytes = (Get-Item -LiteralPath $_).Length; sha256 = Hash-File $_ } })
$verification = [ordered]@{
    schemaVersion = 1; version = $Version; feed = $feedPath; isolatedPackages = $isolatedPackages; publicApiOnly = $true; projectReferences = 0; packageReferences = $packages; lockedRestoreJit = $true; lockedRestoreAot = $true
    lockDirectory = $lockTemplatePath; locksGeneratedThisRun = [bool]$UpdateLocks
    jit = [ordered]@{ processId = $jitReport.ProcessId; nativeAot = $jitReport.NativeAot; checks = $jitReport.Checks.Count; fonts = $jitReport.Fonts.Count; formulas = $jitReport.Formulas.Count }
    aot = [ordered]@{ processId = $aotReport.ProcessId; nativeAot = $aotReport.NativeAot; checks = $aotReport.Checks.Count; fonts = $aotReport.Fonts.Count; formulas = $aotReport.Formulas.Count; executableSha256 = Hash-File $aotExecutable }
    packageDllChecks = $dllChecks; exportParity = $parity; files = $evidence
    limits = @('This public API consumer does not replace the complete compatibility corpus.', 'Actual installed Windows fallback fonts and contextual outlines depend on the host font installation; their provenance is retained.', 'Cache byte counts are conservative managed estimates; native peak memory and old hardware performance are not measured here.', 'AOT resolves the same verified package DLL inputs; its linked native executable is separately hashed, not claimed byte-identical to a managed DLL.')
}
$json = $verification | ConvertTo-Json -Depth 12
Write-Text (Join-Path $runPath 'verification.json') $json
Write-Text (Join-Path $outputPath 'verification.json') $json
Write-Host "Clean local-package consumer passed JIT + Native AOT, 20 locked font hashes, $($parity.Count) identical exports. Evidence: $(Join-Path $outputPath 'verification.json')"
