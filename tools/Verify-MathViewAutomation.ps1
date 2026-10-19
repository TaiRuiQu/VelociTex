param(
    [Parameter(Mandatory=$true)][string]$ApplicationPath,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
# Run with Windows PowerShell 5.1: its WPF UI Automation client is an independent
# desktop process and does not depend on the implementation's managed peer type.
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$application=[IO.Path]::GetFullPath($ApplicationPath)
$output=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
$process=$null
try {
    $process=Start-Process -FilePath $application -ArgumentList @('--p2-smoke',('"'+$output+'"')) -WindowStyle Hidden -PassThru
    $deadline=[DateTime]::UtcNow.AddSeconds(20)
    $elements=$null
    while([DateTime]::UtcNow -lt $deadline -and !$process.HasExited) {
        $process.Refresh()
        if($process.MainWindowHandle -ne [IntPtr]::Zero) {
            $window=[Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
            $condition=New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::ClassNameProperty,'MathView')
            $elements=$window.FindAll([Windows.Automation.TreeScope]::Descendants,$condition)
            if($elements.Count -ge 4){ break }
        }
        Start-Sleep -Milliseconds 80
    }
    if($null -eq $elements -or $elements.Count -ne 4){throw 'The independent UIA client did not find four MathView providers in the real HWND tree.'}
    $values=@()
    foreach($element in $elements) {
        $pattern=$null
        if(!$element.TryGetCurrentPattern([Windows.Automation.ValuePattern]::Pattern,[ref]$pattern)){throw 'MathView has no externally visible UIA Value pattern.'}
        $current=$pattern.Current
        if(!$current.IsReadOnly -or [string]::IsNullOrEmpty($current.Value)){throw 'MathView did not expose a complete, read-only source value.'}
        if($element.Current.ControlType -ne [Windows.Automation.ControlType]::Text){throw 'MathView must expose the Text control type.'}
        if(!$element.Current.IsKeyboardFocusable){throw 'MathView must support keyboard focus for formula copying.'}
        if([string]::IsNullOrEmpty($element.Current.Name) -or !$element.Current.HelpText.Contains('Control+C')){throw 'MathView is missing its accessible name or copy help.'}
        $values += [ordered]@{name=$element.Current.Name;source=$current.Value;readOnly=$current.IsReadOnly;helpText=$element.Current.HelpText;controlType=$element.Current.ControlType.ProgrammaticName}
    }
    if(@($values | Where-Object {$_.source -eq '\sum_{k=1}^{n}\frac{1}{k^2}'}).Count -ne 1){throw 'The independent UIA client did not retrieve the complete summation formula.'}
    $report=[ordered]@{scope='Independent Windows UI Automation client reading the live HWND provider tree';application=$application;processId=$process.Id;clientProcessId=$PID;clientRuntime=[Environment]::Version.ToString();capturedAt=[DateTimeOffset]::Now.ToString('O');providers=$values;screenReaderAudioTested=$false}
    $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output 'uia-client-report.json') -Encoding UTF8
    if(!$process.WaitForExit(45000)){$process.Kill();throw 'The owned acceptance application did not finish.'}
    if(Test-Path -LiteralPath (Join-Path $output 'smoke-error.txt')){throw (Get-Content -LiteralPath (Join-Path $output 'smoke-error.txt') -Raw)}
    if(!(Test-Path -LiteralPath (Join-Path $output 'winui-report.json'))){throw 'The owned UI application did not write its acceptance report.'}
    $liveReport=Get-Content -LiteralPath (Join-Path $output 'winui-report.json') -Raw | ConvertFrom-Json
    if($liveReport.processId -ne $process.Id -or !$liveReport.keyboardCompleteSourceCopy -or !$liveReport.precompiledResultBindingAndClear -or !$liveReport.liveAutomationPeerReadOnlyValue){throw 'The owned UI application did not complete all functional checks in the expected process.'}
    if($liveReport.actualUnloadedEvents -ne 4 -or $liveReport.actualReloadedEvents -ne 4){throw 'The owned UI application did not perform a real unload/reload of every formula.'}
    Write-Host "Independent UIA found $($values.Count) read-only MathView source providers; the owned app completed screenshots and clipboard checks."
}
finally {if($null -ne $process -and !$process.HasExited){$process.Kill()}}
