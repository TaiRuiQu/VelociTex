param([switch]$Check,[switch]$ValidateOnly)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Push-Location $root
try{
    Add-Type -AssemblyName System.IO.Compression
    function RawHash([string]$Path){
        $stream=[IO.File]::OpenRead($Path)
        try{[Convert]::ToHexStringLower([Security.Cryptography.SHA256]::HashData($stream))}finally{$stream.Dispose()}
    }
    function TextHash([string]$Path){
        $text=[IO.File]::ReadAllText($Path).Replace("`r`n","`n")
        [Convert]::ToHexStringLower([Security.Cryptography.SHA256]::HashData([Text.UTF8Encoding]::new($false).GetBytes($text)))
    }
    function Relative([string]$Path){[IO.Path]::GetRelativePath($root,[IO.Path]::GetFullPath($Path)).Replace('\','/')}
    $sources=[Collections.Generic.SortedDictionary[string,string]]::new([StringComparer]::Ordinal)
    $fonts=[Collections.Generic.SortedDictionary[string,string]]::new([StringComparer]::Ordinal)
    $outputs=[Collections.Generic.SortedDictionary[string,string]]::new([StringComparer]::Ordinal)
    function Add-Output([string]$Path,[string]$Expected=''){
        $relative=Relative $Path
        if(!$outputs.ContainsKey($relative)){$outputs[$relative]=RawHash ([IO.Path]::GetFullPath($Path))}
        if($Expected -and $outputs[$relative] -ne $Expected){throw "Output hash differs from actual producer evidence: $relative"}
    }
    $sourcePaths=@(& git ls-files --cached --others --exclude-standard)
    if($LASTEXITCODE -ne 0){throw 'Cannot enumerate versioned source inputs.'}
    foreach($path in ($sourcePaths|Sort-Object -Unique)){
        if($path -match '^src/.*/Resources/Fonts/.*\.ttf$'){$fonts[$path]=RawHash $path;continue}
        if($path -match '^(src|samples|tests|tools|compatibility)/' -or $path -in @('.gitattributes','Directory.Build.props','Directory.Packages.props','NuGet.Config','global.json','VelociTex.slnx','build.ps1','verify-p4.ps1','pack-local.ps1','PACKAGE-README.md','SECURITY.md','THIRD-PARTY-NOTICES.md','LICENSE','docs/package-contract.md','docs/verification/p4-2026-10-19.md','docs/reference/p4-reference.evidence.json','docs/reference/development-hardware.json','docs/reference/development-toolchain.lock.json','docs/reference/katex-0.19.0.lock.json','docs/reference/katex-0.19.0.files.json','docs/reference/font-generation-0.19.0.json') -or $path -match '^licenses/'){
            if($path -notmatch '\.(ttf|woff2|png|jpg|gif|pdb|exe|dll|pdf|bmp)$'){$sources[$path]=TextHash $path}
        }
    }
    if($fonts.Count -ne 20){throw 'Expected the 20 unchanged embedded KaTeX fonts.'}
    $corpusSha=RawHash 'tests/fixtures/p4.json'
    $reference=Get-Content -LiteralPath 'docs/reference/p4-reference.evidence.json' -Raw|ConvertFrom-Json
    if($reference.corpusSha256 -ne $corpusSha -or $reference.oracleCodeSha256 -ne (RawHash 'tools/reference/p1-reference.mjs') -or $reference.valid -ne 3659 -or $reference.invalid -ne 50 -or $reference.measurements -ne 3843){throw 'Final independent reference identity differs.'}
    $runtime=[ordered]@{}
    foreach($name in @('jit','aot')){
        $report=Get-Content -LiteralPath "artifacts/p4/$name/report.json" -Raw|ConvertFrom-Json
        $diff=Get-Content -LiteralPath "artifacts/p4/$name/differential.json" -Raw|ConvertFrom-Json
        if($report.corpusSha256 -ne $corpusSha -or $report.diagnosticRun -or $report.expectationFailures -ne 0 -or $report.exportErrors.Count -or $diff.failures.Count -or $diff.measurements -ne 3843 -or !$diff.semanticComparisonExecuted -or @($diff.presentationChecks|Where-Object {!$_.pass}).Count){throw "Final $name strict mathematical checks did not pass."}
        if(($name -eq 'aot') -ne [bool]$report.nativeAot){throw "Runtime identity differs: $name"}
        $runtime[$name]=[ordered]@{processId=$report.processId;nativeAot=$report.nativeAot;valid=$report.validFixtures;invalid=$report.invalidFixtures;measurements=$diff.measurements;hardFailures=0;presentationChecks=$diff.presentationChecks.Count}
        foreach($file in [IO.Directory]::EnumerateFiles((Join-Path $root "artifacts/p4/$name"),'*',[IO.SearchOption]::AllDirectories)){Add-Output $file}
    }
    foreach($asset in $reference.assets){Add-Output (Join-Path $root $asset.path) $asset.sha256}
    foreach($file in [IO.Directory]::EnumerateFiles((Join-Path $root 'artifacts/p4/reference'),'*',[IO.SearchOption]::AllDirectories)){Add-Output $file}
    $parity=Get-Content -LiteralPath 'artifacts/p4/runtime-parity.json' -Raw|ConvertFrom-Json
    if(!$parity.semanticsGeometryAndDiagnosticResultsEqual -or $parity.identicalExports -ne 11529){throw 'Final actual JIT/AOT output parity did not pass.'}
    foreach($entry in $parity.outputs){foreach($name in @('jit','aot')){Add-Output (Join-Path $root "artifacts/p4/$name/$($entry.path)") $entry.sha256}}
    $image=Get-Content -LiteralPath 'artifacts/p4/jit/image-check.json' -Raw|ConvertFrom-Json
    if($image.measurements -ne 3843 -or @($image.checks|Where-Object {$_.edgeClipping -or $_.nativeToSvg -lt .95 -or $_.svgToNative -lt .95}).Count){throw 'A native/SVG geometry or image clipping check remains.'}
    $audit=Get-Content -LiteralPath 'compatibility/p4-api-audit.json' -Raw|ConvertFrom-Json
    if($audit.summary.included -ne 2939 -or $audit.summary.inputMapped -ne 2939 -or $audit.summary.unmapped -ne 0 -or !$audit.nativeEvidence.hardDifferentialExecuted -or $audit.nativeEvidence.hardFailures -ne 0){throw 'The complete included registration denominator lacks actual hard-check evidence.'}

    $packageReport=Get-Content -LiteralPath 'artifacts/packages/p4/package-report.json' -Raw|ConvertFrom-Json
    $sourceLink=Get-Content -LiteralPath 'artifacts/packages/p4/sourcelink-report.json' -Raw|ConvertFrom-Json
    if($packageReport.version -ne '0.4.0-preview.1' -or !$packageReport.deterministicPayloadRebuildVerified -or !$packageReport.sourceLinkVerified -or !$sourceLink.sourceLinkSyntaxVerified -or $sourceLink.remoteSourceAvailabilityVerified){throw 'Local preview/PDB evidence differs from the recorded boundary.'}
    $packages=[ordered]@{}
    foreach($package in $packageReport.packages){
        Add-Output $package.path $package.sha256
        $symbols=[IO.Path]::ChangeExtension($package.path,'.snupkg');Add-Output $symbols $package.symbolsSha256
        $map=@($sourceLink.packages|Where-Object {$_.id -eq $package.id})[0]
        if(!$map.allOwnedCSharpSourcesEmbeddedAndChecksumVerified -or @($map.documents|Where-Object {!$_.embedded -or !$_.embeddedChecksumMatches}).Count){throw "Embedded PDB sources differ: $($package.id)"}
        Add-Output $map.symbolsPackage $map.symbolsSha256
        $archive=[IO.Compression.ZipFile]::OpenRead($symbols)
        try{
            $entry=$archive.GetEntry($map.pdbEntry);$stream=$entry.Open()
            try{$pdbHash=[Convert]::ToHexStringLower([Security.Cryptography.SHA256]::HashData($stream))}finally{$stream.Dispose()}
            if($pdbHash -ne $map.pdbSha256){throw "Actual portable PDB differs: $($package.id)"}
        }finally{$archive.Dispose()}
        foreach($document in $map.documents){if($document.name -notmatch '[\\/]obj[\\/]' -and $document.name.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){if((RawHash $document.name) -ne $document.checksum){throw "Production source differs from the embedded packaged source: $($document.name)"}}}
        $packages[$package.id]=[ordered]@{version='0.4.0-preview.1';nupkgSha256=$package.sha256;symbolsSha256=$map.symbolsSha256;pdbSha256=$pdbHash;embeddedSourceDocuments=$map.documents.Count;allOwnedCSharpSourcesEmbeddedAndChecksumVerified=$true;remoteSourceAvailabilityVerified=$false}
    }
    foreach($file in [IO.Directory]::EnumerateFiles((Join-Path $root 'artifacts/packages/p4'),'*',[IO.SearchOption]::TopDirectoryOnly)){Add-Output $file}

    $headless=Get-Content -LiteralPath 'artifacts/p4/package-consumer/headless/verification.json' -Raw|ConvertFrom-Json
    if($headless.version -ne '0.4.0-preview.1' -or !$headless.publicApiOnly -or $headless.projectReferences -ne 0 -or !$headless.lockedRestoreJit -or !$headless.lockedRestoreAot -or !$headless.aot.nativeAot -or $headless.jit.checks -ne 141 -or $headless.aot.checks -ne 141 -or $headless.exportParity.Count -ne 30){throw 'Clean headless consumer verification differs.'}
    foreach($file in $headless.files){Add-Output $file.path $file.sha256}
    foreach($file in $headless.exportParity){if(!$file.equal){throw 'Headless actual exports differ.'};Add-Output $file.jitPath $file.sha256;Add-Output $file.aotPath $file.sha256}
    foreach($package in $headless.packageDllChecks){if(!$package.equal){throw 'Consumer package DLL differs.'};Add-Output $package.nupkg $package.packageSha256;Add-Output $package.restoredDll $package.dllSha256;Add-Output $package.jitRuntimeDll $package.dllSha256}
    foreach($directory in @('jit','aot','review')){$folder=Join-Path $headless.runPath $directory;if(Test-Path -LiteralPath $folder){foreach($file in [IO.Directory]::EnumerateFiles($folder,'*',[IO.SearchOption]::AllDirectories)){Add-Output $file}}}
    $winui=Get-Content -LiteralPath 'artifacts/p4/package-consumer/winui/package-consumer-report.json' -Raw|ConvertFrom-Json
    $ui=Get-Content -LiteralPath 'artifacts/p4/package-consumer/winui/aot/winui-report.json' -Raw|ConvertFrom-Json
    $uia=Get-Content -LiteralPath 'artifacts/p4/package-consumer/winui/aot/uia-client-report.json' -Raw|ConvertFrom-Json
    if($winui.version -ne '0.4.0-preview.1' -or !$winui.noProjectReferences -or !$winui.privatePackageCache -or !$ui.nativeAot -or !$ui.keyboardCompleteSourceCopy -or !$ui.precompiledResultBindingAndClear -or !$ui.liveAutomationPeerReadOnlyValue -or $ui.actualUnloadedEvents -ne 4 -or $ui.actualReloadedEvents -ne 4 -or $uia.processId -ne $ui.processId -or $uia.providers.Count -ne 4){throw 'Clean actual WinUI AOT consumer checks differ.'}
    foreach($file in $winui.consumerInputs){Add-Output (Join-Path $winui.consumer $file.path) $file.sha256}
    foreach($file in $winui.publishedFiles){Add-Output (Join-Path $winui.consumer ('publish/'+$file.path)) $file.sha256}
    Add-Output (Join-Path $winui.consumer 'publish/VelociTex.WinUI.PackageConsumer.exe') $ui.executableSha256
    foreach($package in $winui.packages){if($package.nupkgSha256 -ne $packages[$package.id].nupkgSha256 -or $package.restoredNupkgSha256 -ne $package.nupkgSha256){throw 'WinUI consumed a different frozen preview package.'}}
    foreach($directory in @('workload-aot','representative-layout-review','package-consumer/winui/aot')){foreach($file in [IO.Directory]::EnumerateFiles((Join-Path $root "artifacts/p4/$directory"),'*',[IO.SearchOption]::AllDirectories)){Add-Output $file}}
    foreach($file in @('artifacts/p4/package-consumer/headless/verification.json','artifacts/p4/package-consumer/winui/package-consumer-report.json','artifacts/p4/differential-summary.json','artifacts/p4/image-summary.json','artifacts/p4/runtime-parity.json','artifacts/p4/semantic-diagnostic.json','artifacts/p4/publish/VelociTex.P1.Tests.exe')){Add-Output $file}
    $workloads=@();for($i=1;$i -le 5;$i++){$work=Get-Content -LiteralPath "artifacts/p4/workload-aot/run-$i/workload.json" -Raw|ConvertFrom-Json;if(!$work.nativeAot -or $work.oldCpuAcceptance -or $work.overall.samples -ne 6000){throw 'Normal corpus workload boundary differs.'};$workloads+=[ordered]@{processId=$work.processId;samples=$work.overall.samples;p95Ms=$work.overall.p95Ms;minimumThreadManagedBytes=$work.overall.minimumThreadManagedBytes;maximumThreadManagedBytes=$work.overall.maximumThreadManagedBytes}}
    $record=[ordered]@{
        schemaVersion=1;phase='P4';sourceHashPolicy='UTF-8 without BOM, LF canonical current implementation/test/generator/package-tool inputs. Font and output bytes are raw. Historical P2/P3 evidence is not regenerated. Root README/handoff/roadmap narration is outside this implementation snapshot.'
        acceptancePolicy='User explicitly relaxed antialiasing inconsistency on 2026-10-19: correct layout is required. Raw pixel threshold failures remain informational, not relabeled passes; mathematical hard checks, missing glyphs, order, clipping and paint colors remain mandatory.'
        corpusSha256=$corpusSha;oracleCodeSha256=$reference.oracleCodeSha256;includedRegistrationIdentities=2939;unmappedRegistrationIdentities=0;exhaustiveAllParameterBranchesClaimed=$false
        sources=$sources;rawFontFiles=$fonts;runtime=$runtime;identicalExports=11529;rawRasterThresholdFailures=$image.failures.Count;rawRasterThresholdFailuresAreAcceptanceBlock=$false;nativeSvgGeometryAllPass=$true;edgeClipping=0
        packages=$packages;packageSourceBoundary='Actual embedded production C# checksums are verified against current source. Package entry hashes are authoritative for frozen documentation; post-pack SECURITY/package-contract clarification is current source only, not falsely claimed inside those unchanged packages.'
        consumers=[ordered]@{headlessJit=$headless.jit;headlessAot=$headless.aot;headlessIdenticalExports=30;winuiProcessId=$ui.processId;winuiExecutableSha256=$ui.executableSha256;externalUiaClientProcessId=$uia.clientProcessId;actualWinuiScreenshots=10}
        workloads=$workloads;unverified=[ordered]@{oldCpu=$true;lowMemoryHardware=$true;actualMonitorDpiSwitch=$true;hardwareGpuDeviceRemoval=$true;exactNativeGpuPeakLedger=$true;screenReaderAudio=$true;remoteSourceAvailability=$true;externalPublication=$true}
        outputFiles=$outputs
    }
    $target='docs/reference/p4.evidence.json'
    if($ValidateOnly){Write-Host "Validated P4 inputs: $($sources.Count) canonical sources, $($fonts.Count) raw fonts, $($outputs.Count) actual outputs. No evidence file was written."}
    elseif($Check){
        $before=Get-Content -LiteralPath $target -Raw|ConvertFrom-Json -AsHashtable
        foreach($group in @('sources','rawFontFiles','outputFiles')){if($before[$group].Count -ne $record[$group].Count){throw "P4 evidence $group count differs."};foreach($entry in $record[$group].GetEnumerator()){if($before[$group][$entry.Key] -ne $entry.Value){throw "P4 evidence changed: $($entry.Key)"}}}
        foreach($name in @('corpusSha256','oracleCodeSha256','includedRegistrationIdentities','identicalExports','rawRasterThresholdFailures')){if($before[$name] -ne $record[$name]){throw "P4 evidence assertion differs: $name"}}
        Write-Host "PASS P4 evidence: $($sources.Count) canonical sources, $($fonts.Count) raw fonts, $($outputs.Count) actual outputs; both runtimes hard-zero and actual consumers verified."
    }
    else{$record|ConvertTo-Json -Depth 18|Set-Content -LiteralPath $target -Encoding utf8;Write-Host "Recorded P4 evidence: $($sources.Count) canonical sources, $($fonts.Count) raw fonts, $($outputs.Count) actual outputs."}
    $global:LASTEXITCODE=0
}
finally{Pop-Location}
