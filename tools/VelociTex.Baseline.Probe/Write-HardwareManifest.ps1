param([string] $RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path)
$ErrorActionPreference = 'Stop'

if (-not ('VelociTexBaselineHardware' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class VelociTexBaselineHardware
{
    [StructLayout(LayoutKind.Sequential)] public struct MemoryStatus
    {
        public uint Length, Load;
        public ulong TotalPhysical, AvailablePhysical, TotalPageFile, AvailablePageFile, TotalVirtual, AvailableVirtual, AvailableExtendedVirtual;
    }
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GlobalMemoryStatusEx(ref MemoryStatus status);
    [DllImport("user32.dll")] public static extern uint GetDpiForSystem();
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
}
'@
}
$processorKey = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('HARDWARE\DESCRIPTION\System\CentralProcessor\0')
$osKey = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('SOFTWARE\Microsoft\Windows NT\CurrentVersion')
$videoClass = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}')
$adapters = @()
foreach ($subkey in $videoClass.GetSubKeyNames()) {
    if ($subkey -notmatch '^\d{4}$') { continue }
    $adapterKey = $videoClass.OpenSubKey($subkey)
    $description = $adapterKey.GetValue('DriverDesc')
    if ($description) {
        $adapters += @{ description = [string]$description; driverVersion = [string]$adapterKey.GetValue('DriverVersion'); matchingDeviceId = [string]$adapterKey.GetValue('MatchingDeviceId') }
    }
    $adapterKey.Dispose()
}
$memoryStatus = [VelociTexBaselineHardware+MemoryStatus]::new()
$memoryStatus.Length = [System.Runtime.InteropServices.Marshal]::SizeOf($memoryStatus)
if (-not [VelociTexBaselineHardware]::GlobalMemoryStatusEx([ref]$memoryStatus)) { throw 'GlobalMemoryStatusEx failed' }
$manifest = [ordered]@{
    schemaVersion = 1
    recordedAtUtc = [DateTimeOffset]::UtcNow.ToString('O')
    timezone = 'Asia/Shanghai'
    cpu = @{ name = [string]$processorKey.GetValue('ProcessorNameString'); identifier = [string]$processorKey.GetValue('Identifier'); logicalProcessors = [Environment]::ProcessorCount; nominalRegistryMHz = $processorKey.GetValue('~MHz') }
    totalPhysicalMemoryBytes = $memoryStatus.TotalPhysical
    gpuAdaptersFromRegistry = $adapters
    operatingSystem = @{ productNameRegistry = [string]$osKey.GetValue('ProductName'); displayVersion = [string]$osKey.GetValue('DisplayVersion'); build = [string]$osKey.GetValue('CurrentBuildNumber'); revision = $osKey.GetValue('UBR'); processOSVersion = [Environment]::OSVersion.Version.ToString() }
    powerScheme = (& powercfg /GETACTIVESCHEME | Out-String).Trim()
    display = @{ dpiReportedToPowerShellProcess = [VelociTexBaselineHardware]::GetDpiForSystem(); primaryWidthReportedToPowerShellProcess = [VelociTexBaselineHardware]::GetSystemMetrics(0); primaryHeightReportedToPowerShellProcess = [VelociTexBaselineHardware]::GetSystemMetrics(1); monitorCount = [VelociTexBaselineHardware]::GetSystemMetrics(80); note = 'Process DPI awareness can virtualize reported dimensions; native screenshot/DPI probe is authoritative for rendering.' }
    oldCpuReferenceMachine = @{ status = 'unverified'; reason = 'No decade-old reference machine was available. This CPU is not an acceptance substitute.' }
    measurementConditions = 'Local development desktop; no claim of controlled thermal state, quiet background load, fixed clocks, or isolated performance lab. GPU is registry inventory, not proof of selected rendering adapter.'
}
$processorKey.Dispose()
$osKey.Dispose()
$videoClass.Dispose()
$json = $manifest | ConvertTo-Json -Depth 8
$artifactDirectory = Join-Path $RepositoryRoot 'artifacts/baseline'
New-Item -ItemType Directory -Force -Path $artifactDirectory | Out-Null
[IO.File]::WriteAllText((Join-Path $artifactDirectory 'hardware.json'), $json + "`n", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $RepositoryRoot 'docs/reference/development-hardware.json'), $json + "`n", [Text.UTF8Encoding]::new($false))
Write-Output $json
