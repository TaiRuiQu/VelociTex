param(
    [string]$OutputDirectory = 'artifacts/packages/release',
    [ValidatePattern('^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$')][string]$Version = '1.0.0-preview6',
    [switch]$Locked,
    [switch]$VerifyDeterminism
)
$ErrorActionPreference = 'Stop'
$env:DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE = 'true'
$env:MSBuildEnableWorkloadResolver = 'false'
Push-Location $PSScriptRoot
try {
    & ./native/build-core.ps1 -Configuration Release
    if ($LASTEXITCODE -ne 0) { throw 'Native Core build failed; packages would be unusable without the ABI DLL.' }
    Add-Type -AssemblyName System.IO.Compression
    function Read-PackagePayload([string]$Path) {
        $archive = [IO.Compression.ZipFile]::OpenRead($Path)
        try {
            $result = [ordered]@{}
            foreach ($entry in ($archive.Entries | Sort-Object FullName)) {
                $stream = $entry.Open()
                try { $result[$entry.FullName] = [Convert]::ToHexStringLower([Security.Cryptography.SHA256]::HashData($stream)) }
                finally { $stream.Dispose() }
            }
            return $result
        }
        finally { $archive.Dispose() }
    }
    $output = if ([IO.Path]::IsPathRooted($OutputDirectory)) { [IO.Path]::GetFullPath($OutputDirectory) } else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $OutputDirectory)) }
    $projects = @('VelociTex.Core', 'VelociTex.Rendering.Direct2D', 'VelociTex.WinUI')
    foreach ($project in $projects) {
        $arguments = @('pack', "src/$project/$project.csproj", '-c', 'Release', '-p:Platform=x64', '-p:PublishAot=false', "-p:Version=$Version", '-o', $output, '-m:1', '--nologo')
        if ($Locked) { $arguments += '-p:RestoreLockedMode=true' }
        & dotnet @arguments
        if ($LASTEXITCODE -ne 0) { throw "Local packing failed for $project." }
    }
    $determinismRecords = @()
    if ($VerifyDeterminism) {
        $repeatOutput = Join-Path $output 'repeat'
        foreach ($project in $projects) {
            $arguments = @('pack', "src/$project/$project.csproj", '-c', 'Release', '-p:Platform=x64', '-p:PublishAot=false', "-p:Version=$Version", '-p:NoIncremental=true', '-t:Rebuild,Pack', '-o', $repeatOutput, '-m:1', '--nologo')
            if ($Locked) { $arguments += '-p:RestoreLockedMode=true' }
            & dotnet @arguments
            if ($LASTEXITCODE -ne 0) { throw "Repeated local packing failed for $project." }
            foreach ($extension in @('nupkg', 'snupkg')) {
                $name = "$project.$Version.$extension"
                $originalHash = (Get-FileHash -LiteralPath (Join-Path $output $name) -Algorithm SHA256).Hash.ToLowerInvariant()
                $repeatHash = (Get-FileHash -LiteralPath (Join-Path $repeatOutput $name) -Algorithm SHA256).Hash.ToLowerInvariant()
                $originalPayload = Read-PackagePayload (Join-Path $output $name)
                $repeatPayload = Read-PackagePayload (Join-Path $repeatOutput $name)
                if (($originalPayload | ConvertTo-Json -Compress) -ne ($repeatPayload | ConvertTo-Json -Compress)) { throw "The two clean package payloads differ: $name" }
                $determinismRecords += [ordered]@{file=$name;originalSha256=$originalHash;repeatSha256=$repeatHash;zipBytesEqual=($originalHash -eq $repeatHash);payloadsEqual=$true;entrySha256=$originalPayload}
            }
        }
    }
    $records = @()
    foreach ($project in $projects) {
        $path = Join-Path $output "$project.$Version.nupkg"
        $archive = [IO.Compression.ZipFile]::OpenRead($path)
        try {
            $entries = @($archive.Entries | ForEach-Object { $_.FullName })
            foreach ($required in @('LICENSE', 'PACKAGE-README.md', 'THIRD-PARTY-NOTICES.md', 'SECURITY.md', 'licenses/KaTeX-LICENSE.txt', 'licenses/KaTeX-FONTS-OFL.txt', 'docs/package-contract.md')) {
                if ($required -notin $entries) { throw "$path is missing $required." }
            }
            if (!($entries | Where-Object { $_ -like "lib/*/$project.dll" }) -or !($entries | Where-Object { $_ -like "lib/*/$project.xml" })) {
                throw "$path is missing its assembly or XML API documentation."
            }
            if ($entries | Where-Object { $_ -match '(?i)(node_modules|playwright|chrom(e|ium)|typst|\.js$|\.exe$|\.ttf$)' }) {
                throw "$path contains an unexpected development runtime or loose font payload."
            }
            $symbolPath = [IO.Path]::ChangeExtension($path, '.snupkg')
            if (!(Test-Path -LiteralPath $symbolPath)) { throw "Missing symbols package: $symbolPath" }
            $records += [ordered]@{id=$project;version=$Version;path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $path).Length;entries=$entries;symbolsSha256=(Get-FileHash -LiteralPath $symbolPath -Algorithm SHA256).Hash.ToLowerInvariant()}
        }
        finally { $archive.Dispose() }
    }
    $revision = (& git rev-parse HEAD).Trim()
    $dirty = [bool](& git status --porcelain)
    $sourceLinkReport = Join-Path $output 'sourcelink-report.json'
    $inspectionArguments = @('run', '--project', 'tools/VelociTex.PackageInspection/VelociTex.PackageInspection.csproj', '-c', 'Release', '-p:Platform=x64', '-p:PublishAot=false')
    if ($Locked) { $inspectionArguments += '-p:RestoreLockedMode=true' }
    & dotnet @inspectionArguments -- $output $Version $sourceLinkReport
    if ($LASTEXITCODE -ne 0) { throw 'Actual portable-PDB SourceLink verification failed.' }
    $sourceLinks = Get-Content -LiteralPath $sourceLinkReport -Raw | ConvertFrom-Json
    $report = [ordered]@{scope='Local package structure, licenses and actual portable-PDB source maps; no external publication';version=$Version;createdAt=[DateTimeOffset]::Now.ToString('O');sourceCommit=$revision;workingTreeDirty=$dirty;packages=$records;deterministicPayloadRebuildVerified=[bool]$VerifyDeterminism;zipByteReproducible=([bool]$VerifyDeterminism -and @($determinismRecords | Where-Object {!$_.zipBytesEqual}).Count -eq 0);determinismBoundary='All decompressed entry bytes, including DLL/PDB/XML/nuspec, must match. NuGet ZIP metadata timestamps may differ and are reported separately.';deterministicRebuilds=$determinismRecords;sourceLinkVerified=$sourceLinks.sourceLinkSyntaxVerified;sourceLinkReportSha256=(Get-FileHash -LiteralPath $sourceLinkReport -Algorithm SHA256).Hash.ToLowerInvariant();remoteSourceAvailabilityVerified=$false;remoteSourceBoundary='The source-map syntax and embedded source checksums are verified; unpublished local source is not claimed remotely accessible.';externalPublication=$false;cleanConsumerVerification='Run the separate clean package consumers; packing does not imply they passed.'}
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'package-report.json') -Encoding utf8
    Write-Host "Packed and inspected three local previews plus symbols in $output."
}
finally { Pop-Location }
