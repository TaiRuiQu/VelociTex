param()
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $sourcePaths = @(& rg --files src samples tools benchmarks compatibility -g '!**/bin/**' -g '!**/obj/**' -g '!**/node_modules/**' -g '!**/__pycache__/**')
    $sourcePaths += @('.gitattributes', 'global.json', 'Directory.Build.props', 'Directory.Packages.props', 'VelociTex.slnx', 'NuGet.Config', 'build.ps1', 'verify-reference.ps1')
    $sources = [ordered]@{}
    foreach ($file in ($sourcePaths | Sort-Object -Unique)) {
        if ([IO.Path]::GetExtension($file) -eq '.ttf') {
            $digest = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        else {
            $canonicalSource = [IO.File]::ReadAllText((Join-Path (Get-Location) $file)).Replace("`r`n", "`n")
            $digest = [Convert]::ToHexStringLower([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($canonicalSource)))
        }
        $sources[$file.Replace('\', '/')] = $digest
    }
    $outputs = [ordered]@{}
    foreach ($directory in @('artifacts/winui/aot', 'artifacts/native/aot', 'artifacts/native/jit')) {
        foreach ($file in (Get-ChildItem -LiteralPath $directory -File | Sort-Object Name)) {
            $relative = "$directory/$($file.Name)"
            $outputs[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    foreach ($file in @('artifacts/native/image-audit.json', 'artifacts/native/visual-comparison.png', 'artifacts/winui/publish/VelociTex.WinUI.Sample.exe', 'artifacts/native/publish/VelociTex.Native.Probe.exe')) {
        $outputs[$file] = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $record = [ordered]@{
        schemaVersion = 1
        recordedDate = '2026-10-19'
        scope = 'P0 source snapshot and real native/WinUI output correlation. No formula compatibility or old-CPU acceptance.'
        sourceHashPolicy = 'UTF-8 text with LF line endings, matching .gitattributes; TTF bytes unmodified. Output files are always hashed as raw bytes.'
        sourceFiles = $sources
        outputFiles = $outputs
        verification = [ordered]@{
            winuiAotScreenshots = 'Reviewed actual Windows.Graphics.Capture HWND screenshots; light/dark; four surface raster densities; narrow; unload/reload; rejected InlineUIContainer route.'
            selectedParagraphBaseline = 'DirectWrite inline-object callbacks validated numerically and drawn in the real AOT WinUI application.'
            clipboard = 'Computer Use clicked the whole-paragraph copy button; clipboard compared exactly with the complete source.'
            monitorDpiChanges = 'unverified'
            injectedDeviceLoss = 'unverified'
            hardwareGpuDrawing = 'unverified; WARP and WIC tested'
            oldCpu = 'unverified; no suitable machine available'
            completeEngine = 'not implemented'
        }
    }
    $record | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath docs/reference/p0-native-winui.evidence.json -Encoding utf8NoBOM
    Write-Host "Recorded $($sources.Count) source files and $($outputs.Count) output checksums."
}
finally { Pop-Location }
