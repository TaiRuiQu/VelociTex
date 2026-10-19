param(
    [string]$OutputDirectory = 'artifacts/packages/p2',
    [switch]$Locked,
    [switch]$VerifyDeterminism
)
$ErrorActionPreference = 'Stop'
$env:DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE = 'true'
$env:MSBuildEnableWorkloadResolver = 'false'
Push-Location $PSScriptRoot
try {
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
        $arguments = @('pack', "src/$project/$project.csproj", '-c', 'Release', '-p:Platform=x64', '-p:PublishAot=false', '-o', $output, '--nologo')
        if ($Locked) { $arguments += '-p:RestoreLockedMode=true' }
        & dotnet @arguments
        if ($LASTEXITCODE -ne 0) { throw "Local packing failed for $project." }
    }
    $determinismRecords = @()
    if ($VerifyDeterminism) {
        $repeatOutput = Join-Path $output 'repeat'
        foreach ($project in $projects) {
            $arguments = @('pack', "src/$project/$project.csproj", '-c', 'Release', '-p:Platform=x64', '-p:PublishAot=false', '-p:NoIncremental=true', '-t:Rebuild,Pack', '-o', $repeatOutput, '--nologo')
            if ($Locked) { $arguments += '-p:RestoreLockedMode=true' }
            & dotnet @arguments
            if ($LASTEXITCODE -ne 0) { throw "Repeated local packing failed for $project." }
            foreach ($extension in @('nupkg', 'snupkg')) {
                $name = "$project.0.2.0-preview.1.$extension"
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
        $path = Join-Path $output "$project.0.2.0-preview.1.nupkg"
        $archive = [IO.Compression.ZipFile]::OpenRead($path)
        try {
            $entries = @($archive.Entries | ForEach-Object { $_.FullName })
            foreach ($required in @('LICENSE', 'PACKAGE-README.md', 'THIRD-PARTY-NOTICES.md', 'licenses/KaTeX-LICENSE.txt', 'licenses/KaTeX-FONTS-OFL.txt', 'docs/package-contract.md', 'reference/katex-0.19.0.lock.json', 'reference/katex-0.19.0.files.json', 'reference/font-generation-0.19.0.json', 'reference/Directory.Packages.props', 'reference/global.json')) {
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
            $records += [ordered]@{id=$project;version='0.2.0-preview.1';path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $path).Length;entries=$entries;symbolsSha256=(Get-FileHash -LiteralPath $symbolPath -Algorithm SHA256).Hash.ToLowerInvariant()}
        }
        finally { $archive.Dispose() }
    }
    $revision = (& git rev-parse HEAD).Trim()
    $dirty = [bool](& git status --porcelain)
    $report = [ordered]@{scope='Local preview package structure and licenses; no external publication';createdAt=[DateTimeOffset]::Now.ToString('O');sourceCommit=$revision;workingTreeDirty=$dirty;packages=$records;deterministicPayloadRebuildVerified=[bool]$VerifyDeterminism;zipByteReproducible=([bool]$VerifyDeterminism -and @($determinismRecords | Where-Object {!$_.zipBytesEqual}).Count -eq 0);determinismBoundary='All decompressed entry bytes, including DLL/PDB/XML/nuspec, must match. NuGet ZIP metadata timestamps may differ and are reported separately.';deterministicRebuilds=$determinismRecords;sourceLinkVerified=$false;externalPublication=$false;cleanConsumerVerification='Run the separate clean package consumer; this script does not imply it passed.'}
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'package-report.json') -Encoding utf8
    Write-Host "Packed and inspected three local previews plus symbols in $output."
}
finally { Pop-Location }
