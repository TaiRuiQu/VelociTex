# Source syntax and SDK compiler/analyzer checks.
[CmdletBinding()]
param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    $files = @(& git ls-files --cached --others --exclude-standard)
    if ($LASTEXITCODE -ne 0) { throw 'Unable to enumerate repository sources.' }
    foreach ($file in $files) {
        if (!(Test-Path -LiteralPath $file -PathType Leaf)) { continue }
        switch -Regex ($file) {
            '\.ps1$' {
                $tokens = $null
                $parseErrors = $null
                $null = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PWD $file), [ref]$tokens, [ref]$parseErrors)
                if ($parseErrors.Count) { throw "$file`: $($parseErrors -join [Environment]::NewLine)" }
            }
            '\.(csproj|props|targets|slnx|xaml|manifest)$|(^|/)NuGet\.Config$' {
                $null = [xml](Get-Content -LiteralPath $file -Raw)
            }
            # Node validates against the real JSON grammar; Windows PowerShell's
            # ConvertFrom-Json rejects the nesting depth of the fixture corpus.
            '\.json$' {
                & node tools/check-json.mjs $file
                if ($LASTEXITCODE -ne 0) { throw "JSON syntax check failed: $file" }
            }
            '\.mjs$' {
                & node --check $file
                if ($LASTEXITCODE -ne 0) { throw "JavaScript syntax check failed: $file" }
            }
            '\.py$' {
                & python tools/check-python.py $file
                if ($LASTEXITCODE -ne 0) { throw "Python syntax check failed: $file" }
            }
        }
    }
    if (!$SkipBuild) {
        $env:MSBuildEnableWorkloadResolver = 'false'
        & dotnet build VelociTex.slnx -c Release -p:Platform=x64 -p:PublishAot=false -p:RestoreLockedMode=true -warnaserror -m:1 --nologo
        if ($LASTEXITCODE -ne 0) { throw 'Compiler/analyzer lint failed.' }
        & dotnet build tools/VelociTex.PackageInspection/VelociTex.PackageInspection.csproj -c Release -p:Platform=x64 -p:PublishAot=false -p:RestoreLockedMode=true -warnaserror -m:1 --nologo
        if ($LASTEXITCODE -ne 0) { throw 'Package inspection compiler/analyzer lint failed.' }
    }
    Write-Host 'Repository source lint passed.'
}
finally { Pop-Location }
