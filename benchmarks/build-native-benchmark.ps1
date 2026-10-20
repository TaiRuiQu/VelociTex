$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $root 'artifacts/benchmarks-cpp'
New-Item -ItemType Directory -Force $output | Out-Null
$corpusPath = Join-Path $PSScriptRoot 'workloads.json'
$corpus = Get-Content -LiteralPath $corpusPath -Raw | ConvertFrom-Json
$sha = (Get-FileHash -LiteralPath $corpusPath -Algorithm SHA256).Hash.ToLowerInvariant()
$lines = [Collections.Generic.List[string]]::new()
$lines.Add("static const char* workloadSha256 = `"$sha`";")
$lines.Add('static const std::vector<Unit> units = {')
foreach ($workload in @('single', 'long', 'page')) {
    foreach ($case in $corpus.workloads.$workload.cases) {
        $formulas = if ($workload -eq 'page') { @($case.paragraphs | ForEach-Object { $_ | Where-Object { $null -ne $_.latex } }) } else { @($case) }
        $lines.Add("{`"$workload`",`"$($case.id)`",{")
        foreach ($formula in $formulas) {
            if ($formula.latex.Contains(')VT"')) { throw 'Raw string delimiter collision.' }
            $display = if ($formula.displayMode) { 'true' } else { 'false' }
            $lines.Add('{uR"VT(' + $formula.latex + ')VT",' + $display + '},')
        }
        $lines.Add('}},')
    }
}
$lines.Add('};')
[IO.File]::WriteAllLines((Join-Path $output 'native-workloads.inc'), $lines, [Text.UTF8Encoding]::new($false))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$source = Join-Path $PSScriptRoot 'native-core.cpp'
$exe = Join-Path $output 'native-core.exe'
$obj = Join-Path $output 'native-core.obj'
$command = "call `"$vcvars`" >nul && cl /nologo /utf-8 /std:c++20 /O2 /DNDEBUG /EHsc /MT `"$source`" /I`"$output`" /Fo:`"$obj`" /Fe:`"$exe`" /link /NOLOGO"
& cmd.exe /d /s /c $command
if ($LASTEXITCODE) { throw 'Standalone C++ benchmark build failed.' }
