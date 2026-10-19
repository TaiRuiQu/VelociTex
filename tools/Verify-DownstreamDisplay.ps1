param(
    [string]$ApplicationPath='artifacts/p3/final-theme-aot-publish/OhMyStudy.App.exe',
    [string]$OutputDirectory='artifacts/p3/monitored-aot',
    [switch]$Legacy
)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Management
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exe=if([IO.Path]::IsPathRooted($ApplicationPath)){[IO.Path]::GetFullPath($ApplicationPath)}else{[IO.Path]::GetFullPath((Join-Path $repo $ApplicationPath))}
$output=if([IO.Path]::IsPathRooted($OutputDirectory)){[IO.Path]::GetFullPath($OutputDirectory)}else{[IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory))}
New-Item -ItemType Directory -Path $output -Force|Out-Null
$processes=[Collections.Generic.List[object]]::new()
$watcher=[System.Management.ManagementEventWatcher]::new("SELECT * FROM __InstanceCreationEvent WITHIN 0.1 WHERE TargetInstance ISA 'Win32_Process'")
$sourceIdentifier='VelociTexP3-'+[Guid]::NewGuid().ToString('N')
$subscription=Register-ObjectEvent -InputObject $watcher -EventName EventArrived -SourceIdentifier $sourceIdentifier
$watcher.Start()
$previous=[Environment]::GetEnvironmentVariable('OHMYSTUDY_LEGACY_FORMULA_DISPLAY','Process')
try{
    if($Legacy){$env:OHMYSTUDY_LEGACY_FORMULA_DISPLAY='1'}else{Remove-Item Env:OHMYSTUDY_LEGACY_FORMULA_DISPLAY -ErrorAction SilentlyContinue}
    $report=Join-Path $output 'math.txt'
    $owned=Start-Process -FilePath $exe -ArgumentList @('--math-smoke',('"'+$report+'"')) -WindowStyle Hidden -PassThru
    $timer=[Diagnostics.Stopwatch]::StartNew()
    while(!$owned.HasExited){
        foreach($event in @(Get-Event -SourceIdentifier $sourceIdentifier -ErrorAction SilentlyContinue)){
            $item=$event.SourceEventArgs.NewEvent.TargetInstance
            $processes.Add([ordered]@{name=[string]$item.Name;processId=[int]$item.ProcessId;parentProcessId=[int]$item.ParentProcessId})
            Remove-Event -EventIdentifier $event.EventIdentifier
        }
        if($timer.ElapsedMilliseconds -gt 60000){$owned.Kill();throw 'Owned downstream display smoke timed out'}
        Start-Sleep -Milliseconds 50
    }
    $owned.WaitForExit();Start-Sleep -Milliseconds 200
    foreach($event in @(Get-Event -SourceIdentifier $sourceIdentifier -ErrorAction SilentlyContinue)){
        $item=$event.SourceEventArgs.NewEvent.TargetInstance;$processes.Add([ordered]@{name=[string]$item.Name;processId=[int]$item.ProcessId;parentProcessId=[int]$item.ParentProcessId});Remove-Event -EventIdentifier $event.EventIdentifier
    }
    if($owned.ExitCode -ne 0 -or (Get-Content -LiteralPath $report -First 1) -ne 'PASS'){throw 'Downstream display smoke failed'}
    $typst=@($processes|Where-Object {$_.name -ieq 'typst.exe' -and $_.parentProcessId -eq $owned.Id})
    if(!$Legacy -and $typst.Count){throw 'Ordinary native display started Typst'}
    [ordered]@{legacy=[bool]$Legacy;processId=$owned.Id;applicationSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant();exitCode=$owned.ExitCode;typstChildren=$typst.Count;method='WMI __InstanceCreationEvent at100ms, active before owned process launch. This polling event can miss shorter processes; use together with unchanged print tool being deliberately unavailable in native-only acceptance. Privileged ProcessStartTrace was unavailable in this user session.';processStarts=$processes;reportSha256=(Get-FileHash -LiteralPath $report -Algorithm SHA256).Hash.ToLowerInvariant()}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $output 'process-trace.json') -Encoding utf8
    Get-Content -LiteralPath $report
    $global:LASTEXITCODE=0
}
finally{
    [Environment]::SetEnvironmentVariable('OHMYSTUDY_LEGACY_FORMULA_DISPLAY',$previous,'Process')
    $watcher.Stop();Unregister-Event -SourceIdentifier $sourceIdentifier -ErrorAction SilentlyContinue;$watcher.Dispose();if($subscription){Remove-Job -Id $subscription.Id -Force -ErrorAction SilentlyContinue}
}
