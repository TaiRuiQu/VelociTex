param(
    [switch]$Check,
    [string]$DownstreamDirectory = '../oh-my-study'
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$downstream=if([IO.Path]::IsPathRooted($DownstreamDirectory)){[IO.Path]::GetFullPath($DownstreamDirectory)}else{[IO.Path]::GetFullPath((Join-Path $root $DownstreamDirectory))}
Push-Location $root
try{
    Add-Type -AssemblyName System.IO.Compression
    function RawHash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
    function TextHash([string]$Path){
        $value=[IO.File]::ReadAllText($Path).Replace("`r`n","`n")
        [Convert]::ToHexStringLower([Security.Cryptography.SHA256]::HashData([Text.UTF8Encoding]::new($false).GetBytes($value)))
    }
    function CanonicalObject($Value){
        if($Value -is [Collections.IDictionary]){
            $sorted=[ordered]@{};foreach($key in ($Value.Keys|Sort-Object)){$sorted[$key]=CanonicalObject $Value[$key]};return $sorted
        }
        if($Value -is [Collections.IEnumerable] -and $Value -isnot [string]){
            $items=@();foreach($item in $Value){$items+=,(CanonicalObject $item)};return ,$items
        }
        return $Value
    }
    $sources=[ordered]@{}
    $paths=@('docs/verification/p3-2026-10-19.md','tools/Verify-DownstreamDisplay.ps1','tools/Write-P3MigrationPatch.py','tools/Write-P3Evidence.ps1')
    $paths+=@(Get-ChildItem -LiteralPath 'integration/oh-my-study' -File | ForEach-Object{[IO.Path]::GetRelativePath($root,$_.FullName).Replace('\','/')})
    foreach($path in ($paths|Sort-Object)){$sources[$path]=TextHash $path}

    $patchManifest=Get-Content -LiteralPath 'integration/oh-my-study/p3.manifest.json' -Raw|ConvertFrom-Json
    if((RawHash 'integration/oh-my-study/p3.patch') -ne $patchManifest.patchSha256){throw 'P3 patch hash differs from its dirty-baseline manifest.'}
    $guarded=[ordered]@{}
    foreach($file in $patchManifest.files){
        $path=Join-Path $downstream $file.path
        $hash=RawHash $path
        if($hash -ne $file.afterRawSha256){throw "Downstream changed after the migration patch was recorded: $($file.path)"}
        $guarded[$file.path]=$hash
    }

    $packageReport=Get-Content -LiteralPath 'artifacts/packages/p2/package-report.json' -Raw|ConvertFrom-Json
    $assets=Get-Content -LiteralPath (Join-Path $downstream 'src/OhMyStudy.App/obj/project.assets.json') -Raw|ConvertFrom-Json -AsHashtable
    $packageRoots=@($assets.packageFolders.Keys)
    $packages=[ordered]@{}
    foreach($id in @('VelociTex.Core','VelociTex.Rendering.Direct2D','VelociTex.WinUI')){
        $feed=Join-Path $root "artifacts/packages/p2/$id.0.2.0-preview.1.nupkg"
        $record=@($packageReport.packages|Where-Object {$_.id -eq $id})
        if($record.Count -ne 1 -or (RawHash $feed) -ne $record[0].sha256){throw "Frozen P2 feed differs: $id"}
        $packageRoot=$null
        foreach($candidate in $packageRoots){$tryPath=Join-Path $candidate ($id.ToLowerInvariant()+'/0.2.0-preview.1');if(Test-Path -LiteralPath $tryPath){$packageRoot=$tryPath;break}}
        if(!$packageRoot){throw "Downstream did not restore $id from its locked package."}
        $restored=Join-Path $packageRoot ($id.ToLowerInvariant()+'.0.2.0-preview.1.nupkg')
        if((RawHash $restored) -ne $record[0].sha256){throw "Downstream restored a different package payload: $id"}
        $assembly=@(Get-ChildItem -LiteralPath (Join-Path $packageRoot 'lib') -Recurse -File -Filter "$id.dll")
        if($assembly.Count -ne 1){throw "Expected one restored assembly for $id."}
        $archive=[IO.Compression.ZipFile]::OpenRead($feed)
        try{
            $entry=@($archive.Entries|Where-Object {$_.FullName -like "lib/*/$id.dll"})
            if($entry.Count -ne 1){throw "Expected one packaged assembly for $id."}
            $stream=$entry[0].Open();try{$packedHash=[Convert]::ToHexStringLower([Security.Cryptography.SHA256]::HashData($stream))}finally{$stream.Dispose()}
        }
        finally{$archive.Dispose()}
        $assemblyHash=RawHash $assembly[0].FullName
        if($assemblyHash -ne $packedHash){throw "Restored assembly differs from frozen package: $id"}
        $packages[$id]=[ordered]@{version='0.2.0-preview.1';nupkgSha256=$record[0].sha256;restoredNupkgSha256=(RawHash $restored);packagedAssemblySha256=$packedHash;restoredAssemblySha256=$assemblyHash}
    }

    $expectedExe='f0edc85489c94b95bd4b7717f2b0407f4c061ebc7d4871fe9543058e9f95fcd2'
    $exe='artifacts/p3/locked-aot-publish/OhMyStudy.App.exe'
    if((RawHash $exe) -ne $expectedExe){throw 'Final locked AOT executable changed.'}
    $runs=Get-Content -LiteralPath 'artifacts/p3/locked-aot/process-report.json' -Raw|ConvertFrom-Json
    if($runs.sha256 -ne $expectedExe -or @($runs.runs|Where-Object {$_.exitCode -ne 0}).Count){throw 'Final locked AOT smoke identities failed.'}
    foreach($folder in @('locked-native-monitored','locked-legacy-monitored','locked-without-typst')){
        $trace=Get-Content -LiteralPath "artifacts/p3/$folder/process-trace.json" -Raw|ConvertFrom-Json
        if($trace.applicationSha256 -ne $expectedExe -or $trace.exitCode -ne 0){throw "Process trace did not use the final successful executable: $folder"}
        if($folder -ne 'locked-legacy-monitored' -and $trace.typstChildren -ne 0){throw "Native display started Typst: $folder"}
    }
    $unavailable=Get-Content -LiteralPath 'artifacts/p3/locked-without-typst/tool-unavailable-proof.json' -Raw|ConvertFrom-Json
    if(!$unavailable.typstExpectedPathAbsent -or $unavailable.executableSha256 -ne $expectedExe){throw 'Tool-unavailable acceptance proof is missing.'}
    $print=Get-Content -LiteralPath 'artifacts/p3/print-regression/verification.json' -Raw|ConvertFrom-Json
    if($print.DynamicCodeSupported -or $print.Exports.Count -ne 4){throw 'The preserved print probe was not a real AOT four-PDF run.'}

    $outputs=[ordered]@{}
    $directories=@('baseline','locked-aot','locked-native-monitored','locked-legacy-monitored','locked-without-typst','print-regression')
    foreach($directory in $directories){
        foreach($file in (Get-ChildItem -LiteralPath "artifacts/p3/$directory" -File -Recurse|Sort-Object FullName)){
            $relative=[IO.Path]::GetRelativePath($root,$file.FullName).Replace('\','/')
            $outputs[$relative]=RawHash $file.FullName
        }
    }
    foreach($path in @($exe,'artifacts/p3/print-regression.log','artifacts/p3/jit-focused/preview.txt','artifacts/p3/jit-shared-device/preview.txt','artifacts/p3/jit-shared-graphics/preview.txt','artifacts/p3/jit-shared-factory/preview.txt','artifacts/p3/final-aot/math.dark.png','artifacts/p3/final-theme-aot/math.dark.png')){$outputs[$path]=RawHash $path}
    $evidence=[ordered]@{
        schemaVersion=1;phase='P3';sourcePolicy='Only P3 integration/docs/tools canonical UTF-8 LF are hashed here. P4 source changes are intentionally excluded. Downstream guarded files, outputs and packages use exact raw bytes.'
        sources=$sources;downstreamGuardedFiles=$guarded;frozenPackages=$packages;finalExecutableSha256=$expectedExe
        verification=[ordered]@{lockedDebugBuild=$true;lockedAotPublish=$true;postAotOrdinaryLockedRestore=$true;dirtyBaselinePatchRehearsed=$true;actualScreenshots=33;nativeAndLegacySameExecutable=$true;typstUnavailableNativeRun=$true;printAotPdfs=4;wmiProcessPollingMilliseconds=100;privilegedKernelProcessTrace=$false;oldCpu=$false;actualMonitorDpiSwitch=$false;screenReaderAudio=$false;hardwareGpuDeviceRemoval=$false;exactGpuPeakLedger=$false}
        outputFiles=$outputs
    }
    $path='docs/reference/p3.evidence.json'
    if($Check){
        $previous=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json -AsHashtable
        $left=CanonicalObject $previous|ConvertTo-Json -Depth 20 -Compress
        $right=CanonicalObject $evidence|ConvertTo-Json -Depth 20 -Compress
        if($left -ne $right){throw 'P3 evidence differs from current guarded sources, packages or outputs.'}
        Write-Host "PASS P3 evidence: $($sources.Count) P3 sources, $($guarded.Count) downstream paths, 3 frozen packages, $($outputs.Count) outputs."
    }
    else{
        $evidence|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $path -Encoding utf8
        Write-Host "Recorded P3 evidence: $($sources.Count) P3 sources, $($guarded.Count) downstream paths, 3 frozen packages, $($outputs.Count) outputs."
    }
    $global:LASTEXITCODE=0
}
finally{Pop-Location}
