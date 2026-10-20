param(
    [string]$Out = "artifacts/native",
    [string]$Configuration = "Release"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if (!(Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }
$outDir = Join-Path $root $Out
New-Item -ItemType Directory -Force $outDir | Out-Null
$source = Join-Path $PSScriptRoot "VelociTex.NativeMath.cpp"
$obj = Join-Path $outDir "VelociTex.NativeMath.obj"
$dll = Join-Path $outDir "VelociTex.NativeMath.dll"
$flags = if ($Configuration -eq "Release") { "/O2 /Ob3 /GL /DNDEBUG" } else { "/Od /Zi" }
$command = "call `"$vcvars`" >nul && cl /nologo /utf-8 /std:c++20 /EHsc /MT $flags /LD `"$source`" /Fo:`"$obj`" /Fe:`"$dll`" /link /EXPORT:vt_mathml /EXPORT:vt_mathml2 /EXPORT:vt_mathml_supported /EXPORT:vt_version"
cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw "cl failed with exit code $LASTEXITCODE" }
Get-Item $dll | Select-Object FullName, Length
