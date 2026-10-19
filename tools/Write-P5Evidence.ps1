param([switch]$Check,[switch]$ValidateOnly)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Push-Location $root
try{
    Add-Type -AssemblyName System.IO.Compression
    function Hash([string]$Path){$stream=[IO.File]::OpenRead($Path);try{[Convert]::ToHexStringLower([Security.Cryptography.SHA256]::HashData($stream))}finally{$stream.Dispose()}}
    function TextHash([string]$Path){[Convert]::ToHexStringLower([Security.Cryptography.SHA256]::HashData([Text.UTF8Encoding]::new($false).GetBytes([IO.File]::ReadAllText($Path).Replace("`r`n","`n"))))}
    function Relative([string]$Path){[IO.Path]::GetRelativePath($root,[IO.Path]::GetFullPath($Path)).Replace('\','/')}
    $sources=[Collections.Generic.SortedDictionary[string,string]]::new([StringComparer]::Ordinal)
    $outputs=[Collections.Generic.SortedDictionary[string,string]]::new([StringComparer]::Ordinal)
    $fontHashes=[Collections.Generic.SortedDictionary[string,string]]::new([StringComparer]::Ordinal)
    function Add-Output([string]$Path,[string]$Expected=''){
        $relative=Relative $Path
        if(!$outputs.ContainsKey($relative)){$outputs[$relative]=Hash ([IO.Path]::GetFullPath($Path))}
        if($Expected -and $outputs[$relative] -ne $Expected){throw "Actual release-preparation output differs: $relative"}
    }
    $paths=@(& git ls-files --cached --others --exclude-standard)
    if($LASTEXITCODE -ne 0){throw 'Cannot enumerate candidate source inputs.'}
    foreach($path in ($paths|Sort-Object -Unique)){
        if($path -match '^src/.*/Resources/Fonts/.*\.ttf$'){$fontHashes[$path]=Hash $path;continue}
        if($path -match '^src/.*\.(cs|csproj|props|json)$' -or $path -match '^samples/.*\.(cs|csproj|xaml|manifest)$' -or $path -match '^samples/.*/package-locks/1\.0\.0-rc\.1/' -or $path -match '^integration/oh-my-study/p5\.' -or $path -in @('Directory.Build.props','Directory.Packages.props','global.json','NuGet.Config','pack-local.ps1','tools/Verify-PackageConsumer.ps1','tools/Verify-WinUIPackageConsumer.ps1','tools/Verify-MathViewAutomation.ps1','tools/Inspect-PackageSourceLink.ps1','tools/Write-P5Evidence.ps1','docs/verification/p5-2026-10-19.md','docs/package-contract.md','docs/release-checklist.md','docs/agent-handoff.md','docs/agent-handoff.zh-CN.md','docs/roadmap.md','docs/roadmap.zh-CN.md','README.md','README.zh-CN.md','PACKAGE-README.md','SECURITY.md','CHANGELOG.md','THIRD-PARTY-NOTICES.md','LICENSE') -or $path -match '^licenses/'){$sources[$path]=TextHash $path}
    }
    $sources['integration/oh-my-study/README.md']=TextHash 'integration/oh-my-study/README.md'
    $p4=Get-Content -LiteralPath 'docs/reference/p4.evidence.json' -Raw|ConvertFrom-Json -AsHashtable
    $reusedMathSources=[ordered]@{}
    foreach($entry in $p4.sources.GetEnumerator()){
        if($entry.Key -match '^src/.*\.cs$'){
            $actual=TextHash $entry.Key
            if($actual -ne $entry.Value){throw "Mathematical production source changed after P4 acceptance: $($entry.Key)"}
            $reusedMathSources[$entry.Key]=$actual
        }
    }
    if($fontHashes.Count -ne 20){throw 'Candidate must keep exactly the 20 original fonts.'}
    foreach($font in $fontHashes.GetEnumerator()){if($p4.rawFontFiles[$font.Key] -ne $font.Value){throw "Candidate font changed: $($font.Key)"}}
    foreach($path in @('artifacts/p4/jit/report.json','artifacts/p4/aot/report.json','artifacts/p4/jit/differential.json','artifacts/p4/aot/differential.json','artifacts/p4/runtime-parity.json','artifacts/p4/reference/reference.json','docs/reference/p4-reference.evidence.json')){
        $expected=$p4.outputFiles[$path]
        if(!$expected -and $path -eq 'docs/reference/p4-reference.evidence.json'){$expected=$null}
        Add-Output $path $expected
    }
    $version='1.0.0-rc.1';$revision='1b0c77774418fd349c36b8b4cd0c89a135f70fba'
    $pack=Get-Content -LiteralPath 'artifacts/packages/p5/package-report.json' -Raw|ConvertFrom-Json
    $link=Get-Content -LiteralPath 'artifacts/packages/p5/sourcelink-report.json' -Raw|ConvertFrom-Json
    $priorLink=Get-Content -LiteralPath 'artifacts/packages/p4/sourcelink-report.json' -Raw|ConvertFrom-Json
    if($pack.version -ne $version -or $pack.sourceCommit -ne $revision -or $pack.workingTreeDirty -or !$pack.deterministicPayloadRebuildVerified -or !$pack.sourceLinkVerified -or !$link.sourceLinkSyntaxVerified -or $link.remoteSourceAvailabilityVerified -or $pack.externalPublication){throw 'The candidate is not the frozen clean local P4 revision with verified local source maps.'}
    $packages=[ordered]@{}
    foreach($package in $pack.packages){
        Add-Output $package.path $package.sha256
        $map=@($link.packages|Where-Object {$_.id -eq $package.id})[0]
        $prior=@($priorLink.packages|Where-Object {$_.id -eq $package.id})[0]
        if(!$map.allOwnedCSharpSourcesEmbeddedAndChecksumVerified -or $map.embeddedOwnedSourceAggregateSha256 -ne $prior.embeddedOwnedSourceAggregateSha256 -or @($map.sourceMapRevisions|Where-Object {$_ -ne $revision}).Count){throw "Candidate source differs from the actual P4 implementation or revision map: $($package.id)"}
        Add-Output $map.symbolsPackage $map.symbolsSha256
        $archive=[IO.Compression.ZipFile]::OpenRead($map.symbolsPackage)
        try{$entry=$archive.GetEntry($map.pdbEntry);$stream=$entry.Open();try{$pdb=[Convert]::ToHexStringLower([Security.Cryptography.SHA256]::HashData($stream))}finally{$stream.Dispose()};if($pdb -ne $map.pdbSha256){throw 'Candidate actual portable PDB differs from its report.'}}finally{$archive.Dispose()}
        foreach($document in $map.documents){if(!$document.embedded -or !$document.embeddedChecksumMatches){throw 'Missing or unverified candidate embedded source.'};if($document.name -notmatch '[\\/]obj[\\/]' -and $document.name.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){if((Hash $document.name) -ne $document.checksum){throw "Candidate embedded source does not match current production code: $($document.name)"}}}
        $packages[$package.id]=[ordered]@{version=$version;nupkgSha256=$package.sha256;symbolsSha256=$map.symbolsSha256;pdbSha256=$pdb;embeddedOwnedSourceAggregateSha256=$map.embeddedOwnedSourceAggregateSha256;embeddedProductionSourceIdenticalToP4=$true;sourceMapRevision=$revision;remoteSourceAvailabilityVerified=$false}
    }
    foreach($file in [IO.Directory]::EnumerateFiles((Join-Path $root 'artifacts/packages/p5'),'*',[IO.SearchOption]::AllDirectories)){Add-Output $file}
    $headless=Get-Content -LiteralPath 'artifacts/p5/package-consumer/headless/verification.json' -Raw|ConvertFrom-Json
    if($headless.version -ne $version -or !$headless.publicApiOnly -or $headless.projectReferences -ne 0 -or !$headless.lockedRestoreJit -or !$headless.lockedRestoreAot -or !$headless.aot.nativeAot -or $headless.jit.checks -ne 141 -or $headless.aot.checks -ne 141 -or $headless.exportParity.Count -ne 30){throw 'Candidate clean headless consumer is incomplete.'}
    foreach($file in $headless.files){Add-Output $file.path $file.sha256}
    foreach($file in $headless.exportParity){if(!$file.equal){throw 'Candidate JIT/AOT exports differ.'};Add-Output $file.jitPath $file.sha256;Add-Output $file.aotPath $file.sha256}
    foreach($package in $headless.packageDllChecks){if(!$package.equal -or $package.packageSha256 -ne $packages[$package.id].nupkgSha256){throw 'Candidate consumer used different package bytes.'};Add-Output $package.nupkg $package.packageSha256;Add-Output $package.restoredDll $package.dllSha256;Add-Output $package.jitRuntimeDll $package.dllSha256}
    $winui=Get-Content -LiteralPath 'artifacts/p5/package-consumer/winui/package-consumer-report.json' -Raw|ConvertFrom-Json
    $ui=Get-Content -LiteralPath 'artifacts/p5/package-consumer/winui/aot/winui-report.json' -Raw|ConvertFrom-Json
    $uia=Get-Content -LiteralPath 'artifacts/p5/package-consumer/winui/aot/uia-client-report.json' -Raw|ConvertFrom-Json
    if($winui.version -ne $version -or !$winui.noProjectReferences -or !$winui.privatePackageCache -or !$ui.nativeAot -or !$ui.keyboardCompleteSourceCopy -or !$ui.precompiledResultBindingAndClear -or !$ui.liveAutomationPeerReadOnlyValue -or $ui.actualUnloadedEvents -ne 4 -or $ui.actualReloadedEvents -ne 4 -or $uia.processId -ne $ui.processId -or $uia.providers.Count -ne 4){throw 'Candidate actual WinUI AOT consumer is incomplete.'}
    foreach($file in $winui.consumerInputs){Add-Output (Join-Path $winui.consumer $file.path) $file.sha256}
    foreach($file in $winui.publishedFiles){Add-Output (Join-Path $winui.consumer ('publish/'+$file.path)) $file.sha256}
    Add-Output (Join-Path $winui.consumer 'publish/VelociTex.WinUI.PackageConsumer.exe') $ui.executableSha256
    foreach($package in $winui.packages){if($package.nupkgSha256 -ne $packages[$package.id].nupkgSha256 -or $package.restoredNupkgSha256 -ne $package.nupkgSha256){throw 'Candidate WinUI restored different bytes.'}}
    foreach($directory in @('jit','aot','review')){$folder=Join-Path $headless.runPath $directory;if(Test-Path -LiteralPath $folder){foreach($file in [IO.Directory]::EnumerateFiles($folder,'*',[IO.SearchOption]::AllDirectories)){Add-Output $file}}}
    foreach($file in [IO.Directory]::EnumerateFiles((Join-Path $root 'artifacts/p5/package-consumer/winui/aot'),'*',[IO.SearchOption]::AllDirectories)){Add-Output $file}
    foreach($path in @('artifacts/p5/package-consumer/headless/verification.json','artifacts/p5/package-consumer/winui/package-consumer-report.json','artifacts/packages/p5/package-report.json','artifacts/packages/p5/sourcelink-report.json')){Add-Output $path}
    $migration=Get-Content -LiteralPath 'integration/oh-my-study/p5.manifest.json' -Raw|ConvertFrom-Json
    if((Hash 'integration/oh-my-study/p5.patch') -ne $migration.patchSha256 -or !$migration.rehearsal.gitApplyCheckPassed -or !$migration.rehearsal.allAppliedCanonicalHashesMatch){throw 'Candidate downstream dirty-baseline delta was not independently rehearsed.'}
    $downstream=[ordered]@{}
    foreach($file in $migration.files){$path=Join-Path $migration.downstream $file.path;if((Hash $path) -ne $file.afterRawSha256){throw "Candidate downstream changed after acceptance: $($file.path)"};$downstream[$file.path]=$file.afterRawSha256}
    $downstreamRun=Get-Content -LiteralPath 'artifacts/p5/downstream-aot/downstream-runtime.json' -Raw|ConvertFrom-Json
    $desktop=Get-Content -LiteralPath 'artifacts/p5/downstream-aot/native-aot-desktop.json' -Raw|ConvertFrom-Json
    if($downstreamRun.version -ne $version -or !$downstreamRun.nativeAot -or !$downstreamRun.passed -or !$downstreamRun.sourceProductionMatchesP4 -or $downstreamRun.sourceCommit -ne $revision -or $downstreamRun.screenshotCount -ne 33 -or !$downstreamRun.nativeAotEvidence.runtimeFeatureIsNativeAot -or $downstreamRun.nativeAotEvidence.peClrHeaderRva -ne 0 -or !$desktop.IsNativeAot -or !$desktop.Passed){throw 'Actual downstream candidate Native AOT/version/runtime checks did not pass.'}
    Add-Output $downstreamRun.applicationPath $downstreamRun.applicationSha256
    Add-Output $downstreamRun.nativeAotEvidence.desktopReport $downstreamRun.nativeAotEvidence.desktopReportSha256
    Add-Output $downstreamRun.nativeAotEvidence.nativeCompilerResponse $downstreamRun.nativeAotEvidence.nativeCompilerResponseSha256
    foreach($name in @('math','preview','withoutTypst')){
        $smoke=$downstreamRun.smokes.$name
        if(!$smoke.passed -or (Get-Content -LiteralPath $smoke.report -First 1) -ne 'PASS'){throw "Actual downstream smoke failed: $name"}
        Add-Output $smoke.report $smoke.reportSha256
    }
    $mathTrace=Get-Content -LiteralPath $downstreamRun.smokes.math.processTrace -Raw|ConvertFrom-Json
    $previewTrace=Get-Content -LiteralPath $downstreamRun.smokes.preview.processTrace -Raw|ConvertFrom-Json
    if($mathTrace.processId -ne $downstreamRun.smokes.math.processId -or $previewTrace.processId -ne $downstreamRun.smokes.preview.processId -or $mathTrace.applicationSha256 -ne $downstreamRun.applicationSha256 -or $previewTrace.applicationSha256 -ne $downstreamRun.applicationSha256 -or $mathTrace.exitCode -ne 0 -or $previewTrace.exitCode -ne 0 -or $previewTrace.coreclrLoaded -or $mathTrace.typstChildren -ne 0){throw 'Downstream smoke process/executable identities differ.'}
    if(!$downstreamRun.smokes.withoutTypst.directoryAbsentBeforeLaunch -or !$downstreamRun.smokes.withoutTypst.directoryAbsentAfterExit -or !$downstreamRun.smokes.withoutTypst.toolRestored){throw 'Actual tool-unavailable downstream proof is incomplete.'}
    Add-Output $downstreamRun.smokes.withoutTypst.proof $downstreamRun.smokes.withoutTypst.proofSha256
    $absence=Get-Content -LiteralPath $downstreamRun.smokes.withoutTypst.proof -Raw|ConvertFrom-Json
    if(!$absence.smokePassed -or !$absence.restored -or !$absence.typstDirectoryAbsentBeforeLaunch -or !$absence.typstDirectoryAbsentAfterExit){throw 'The owned Typst tool was not absent and restored as recorded.'}
    foreach($tool in $absence.toolFilesBefore){$restored=@($absence.toolFilesAfter|Where-Object {$_.path -eq $tool.path})[0];if($restored.sha256 -ne $tool.sha256){throw 'The owned printing tool was not restored byte-for-byte.'};Add-Output (Join-Path $absence.toolPath $tool.path) $tool.sha256}
    foreach($package in $downstreamRun.packages){
        if($package.version -ne $version -or !$package.restoredDllMatchesPackage -or !$package.nativeCompilerReferenced -or $package.packageSha256 -ne $packages[$package.id].nupkgSha256){throw 'Downstream compiler did not consume the frozen RC bytes.'}
        Add-Output $package.packagePath $package.packageSha256;Add-Output $package.restoredDllPath $package.dllSha256
        foreach($copy in $package.buildCopies){if(!$copy.matchesPackage){throw 'Downstream build copy differs from RC package.'};Add-Output $copy.path $package.dllSha256}
    }
    foreach($screenshot in $downstreamRun.screenshots){Add-Output $screenshot.path $screenshot.sha256}
    foreach($directory in @('downstream-aot','downstream-without-typst')){foreach($file in [IO.Directory]::EnumerateFiles((Join-Path $root "artifacts/p5/$directory"),'*',[IO.SearchOption]::AllDirectories)){Add-Output $file}}
    $record=[ordered]@{
        schemaVersion=1;phase='P5';candidateVersion=$version;status='local-preparation-complete';formalOnePointZeroExit='pending-unverified-hardware-platform-and-audio-gates';sourceRevision=$revision;externalPublication=$false
        sourceHashPolicy='Current production C# and candidate tooling/docs/locks canonical UTF-8 LF; raw fonts, package payloads, physical consumers and downstream artifacts retain exact bytes. P4 is reused only with equal embedded production-source checksums.'
        sources=$sources;rawFontFiles=$fontHashes;reusedP4=[ordered]@{evidenceSha256=(Hash 'docs/reference/p4.evidence.json');mathematicalSourceFiles=$reusedMathSources;valid=3659;invalid=50;measurementsPerRuntime=3843;hardFailures=0;identicalExports=11529;candidateFullMatrixRerunClaimed=$false}
        packages=$packages;deterministicDecompressedPayloads=$true;zipByteReproducible=$pack.zipByteReproducible;sourceLinkSyntaxVerified=$true;remoteSourceAvailabilityVerified=$false
        consumers=[ordered]@{headlessJit=$headless.jit;headlessAot=$headless.aot;identicalHeadlessExports=30;winuiProcessId=$ui.processId;winuiExecutableSha256=$ui.executableSha256;independentUiaClientProcessId=$uia.clientProcessId;actualWinuiScreenshots=10};downstreamGuardedFiles=$downstream
        downstream=[ordered]@{version=$version;nativeAot=$true;executableSha256=$downstreamRun.applicationSha256;mathProcessId=$downstreamRun.smokes.math.processId;previewProcessId=$downstreamRun.smokes.preview.processId;nativeAotRuntimeProofProcessId=$downstreamRun.nativeAotEvidence.desktopProcessId;typstUnavailableProcessId=$downstreamRun.smokes.withoutTypst.processId;actualMathPreviewScreenshots=33;sourcePreservedDirtyBaseline=$true;downstreamIndexTouched=$false}
        pendingFormalGates=@('old CPU and low-memory hardware','physical monitor DPI changes','minimum supported Windows 10 real machine','hardware GPU/device removal and exact native/GPU peak ledger','screen-reader audio','remote source availability and separately authorized external publication')
        userAntialiasingPolicy='Antialiasing inconsistency is non-blocking by explicit user instruction. Layout, semantics, missing glyphs, ordering, clipping and lost colors remain mandatory; this does not waive other formal gates.'
        outputFiles=$outputs
    }
    $target='docs/reference/p5.evidence.json'
    if($ValidateOnly){Write-Host "Validated P5 candidate: $($sources.Count) sources, 20 fonts, 3 packages, $($outputs.Count) physical outputs. No manifest was written."}
    elseif($Check){$before=Get-Content -LiteralPath $target -Raw|ConvertFrom-Json -AsHashtable;foreach($group in @('sources','rawFontFiles','outputFiles')){if($before[$group].Count -ne $record[$group].Count){throw "P5 $group count changed."};foreach($entry in $record[$group].GetEnumerator()){if($before[$group][$entry.Key] -ne $entry.Value){throw "P5 evidence changed: $($entry.Key)"}}};if($before.candidateVersion -ne $version -or $before.formalOnePointZeroExit -ne $record.formalOnePointZeroExit){throw 'P5 gate status differs.'};Write-Host "PASS P5 local preparation: $($sources.Count) sources, 20 fonts, 3 candidates, $($outputs.Count) physical outputs. Formal 1.0 hardware/platform/audio gates remain pending."}
    else{$record|ConvertTo-Json -Depth 18|Set-Content -LiteralPath $target -Encoding utf8;Write-Host "Recorded P5 local preparation: $($sources.Count) sources, 20 fonts, $($outputs.Count) outputs; formal 1.0 exit remains pending."}
    $global:LASTEXITCODE=0
}
finally{Pop-Location}
