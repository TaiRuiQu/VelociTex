[CmdletBinding()]
param(
    [string]$Out = 'artifacts/core-native',
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [string]$VcVars
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

# Determine platform and build approach
$isWindows = $PSVersionTable.PSVersion.Major -lt 6 -or $PSVersionTable.Platform -eq 'Win32NT' -or $IsWindows
$isLinux = $PSVersionTable.Platform -eq 'Unix' -and !$IsMacOS

if ($isWindows) {
    # Windows MSVC build
    if (!$VcVars) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($installation) { $VcVars = Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat' }
        }
    }
    if (!$VcVars -or !(Test-Path -LiteralPath $VcVars)) {
        throw 'Visual Studio C++ x64 build tools are required. Supply -VcVars if discovery is unavailable.'
    }

    $outputPath = if ([IO.Path]::IsPathRooted($Out)) { [IO.Path]::GetFullPath($Out) } else { [IO.Path]::GetFullPath((Join-Path $repoRoot $Out)) }
    New-Item -ItemType Directory -Force $outputPath | Out-Null

    $files = @('types.cpp', 'lexer.cpp', 'macros.cpp', 'parser.cpp', 'cache.cpp', 'layout.cpp', 'geometry.cpp', 'mathml.cpp', 'svg.cpp', 'core.cpp', 'api.cpp', 'generated/data.cpp', 'generated/svg_data.cpp')
    $sources = ($files | ForEach-Object { '"' + (Join-Path $PSScriptRoot "core/$_") + '"' }) -join ' '
    $flags = if ($Configuration -eq 'Release') { '/O2 /DNDEBUG' } else { '/Od /Zi' }
    $dll = Join-Path $outputPath 'VelociTex.Core.Native.dll'
    $objectPath = $outputPath.Replace('\', '/') + '/'
    $command = "call `"$VcVars`" >nul && cl /nologo /utf-8 /std:c++20 /EHsc /W4 /fp:precise /MT $flags /LD $sources /Fo:`"$objectPath`" /Fe:`"$dll`" /link /NOLOGO"
    & cmd.exe /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw "Native Core build failed with exit code $LASTEXITCODE." }
    Write-Host "Native Core built at $dll"
} elseif ($isLinux) {
    # Linux CMake build
    $outputPath = if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $repoRoot $Out }
    New-Item -ItemType Directory -Force $outputPath | Out-Null

    $buildDir = Join-Path $outputPath 'build'
    New-Item -ItemType Directory -Force $buildDir | Out-Null

    $cmakeArgs = @(
        '-S', $PSScriptRoot
        '-B', $buildDir
        '-DCMAKE_BUILD_TYPE=' + $Configuration
        '-DCMAKE_POSITION_INDEPENDENT_CODE=ON'
    )

    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }

    & cmake --build $buildDir --config $Configuration
    if ($LASTEXITCODE -ne 0) { throw 'Native Core build failed.' }

    # Copy the built library to the output directory
    $libPattern = Join-Path $buildDir 'libVelociTex.Core.Native.so*'
    $libs = @(Get-ChildItem -Path $libPattern -ErrorAction SilentlyContinue)
    if ($libs.Count -gt 0) {
        foreach ($lib in $libs) {
            Copy-Item $lib.FullName $outputPath -Force
        }
        Write-Host "Native Core built at $outputPath"
    } else {
        throw 'Native Core library not found after build.'
    }
} else {
    throw 'Unsupported platform for native build.'
}
