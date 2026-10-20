# Compile the C++ core into a static library for the .NET WASM runtime.
#
# The browser app cannot load a DLL: native code must be linked into the runtime's
# own Wasm module. VelociTex.Wasm.csproj references this archive through
# NativeFileReference, so the .NET SDK links it into dotnet.native.wasm and
# records its C ABI entry points in the P/Invoke table it generates from the
# managed [LibraryImport] declarations.
#
# The file name is the P/Invoke module name: the SDK derives the module from the
# archive name (minus the lib prefix and .a suffix), so it must match exactly.
#
# Emscripten resolves the C runtime and libc++ during the SDK's final link, so
# this only compiles translation units and archives them. Exceptions are compiled
# as Wasm exception handling (-fwasm-exceptions) to match the runtime link.
[CmdletBinding()]
param(
    [string]$Out = 'artifacts/wasm/native',
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$coreDir = Join-Path $PSScriptRoot 'core'
$outDir = if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $root $Out }
$result = Join-Path $outDir 'VelociTex.Core.Native.a'

# Locate the .NET-shipped Emscripten toolchain so CI and developer machines use
# the exact compiler the WASM runtime links with.
$dotnetRoot = if ($env:DOTNET_ROOT) { $env:DOTNET_ROOT } else { Split-Path (Get-Command dotnet).Source }
$packs = Join-Path $dotnetRoot 'packs'
function Find-Pack([string]$pattern) {
    $pack = Get-ChildItem $packs -Directory -Filter $pattern -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if (!$pack) { return $null }
    $version = Get-ChildItem $pack.FullName -Directory |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (!$version) { return $null }
    return $version.FullName
}
$sdkPack = Find-Pack 'Microsoft.NET.Runtime.Emscripten.*.Sdk.*'
$cachePack = Find-Pack 'Microsoft.NET.Runtime.Emscripten.*.Cache.*'
$nodePack = Find-Pack 'Microsoft.NET.Runtime.Emscripten.*.Node.*'
$pythonPack = Find-Pack 'Microsoft.NET.Runtime.Emscripten.*.Python.*'
if (!$sdkPack) { throw 'The .NET wasm-tools Emscripten SDK pack was not found. Run: dotnet workload install wasm-tools' }
$toolchain = Join-Path $sdkPack 'tools'
$emcc = Join-Path $toolchain 'emscripten/emcc.bat'
if (!(Test-Path -LiteralPath $emcc)) { $emcc = Join-Path $toolchain 'emscripten/emcc' }
if (!(Test-Path -LiteralPath $emcc)) { throw "emcc was not found in $toolchain." }
$llvmAr = Join-Path $toolchain 'bin/llvm-ar.exe'
if (!(Test-Path -LiteralPath $llvmAr)) { $llvmAr = Join-Path $toolchain 'bin/llvm-ar' }

$env:DOTNET_EMSCRIPTEN_LLVM_ROOT = Join-Path $toolchain 'bin'
$env:DOTNET_EMSCRIPTEN_BINARYEN_ROOT = $toolchain
if ($cachePack -and (Test-Path (Join-Path $cachePack 'tools/emscripten/cache'))) {
    $env:FROZEN_CACHE = '1'
    $env:EM_CACHE = Join-Path $cachePack 'tools/emscripten/cache'
} else {
    $env:FROZEN_CACHE = ''
    $env:EM_CACHE = Join-Path $outDir 'emcache'
}
if ($nodePack) { $env:DOTNET_EMSCRIPTEN_NODE_JS = Join-Path (Join-Path (Join-Path $nodePack 'tools') 'bin') 'node.exe' }
if ($pythonPack) { $env:PATH = (Join-Path (Join-Path $pythonPack 'tools') 'bin') + [IO.Path]::PathSeparator + $env:PATH }

New-Item -ItemType Directory -Force $outDir | Out-Null
$objects = Join-Path $outDir 'obj'
if (Test-Path -LiteralPath $objects) { Remove-Item -LiteralPath $objects -Recurse -Force }
New-Item -ItemType Directory -Force $objects | Out-Null

$sources = @(
    'types', 'lexer', 'macros', 'parser', 'cache', 'layout', 'geometry', 'mathml', 'svg', 'core',
    'generated/data', 'generated/svg_data', 'api'
)
# Every C ABI entry point declared in api.h is imported by the managed side, so a
# missing one fails the SDK's link. Collect them to verify the archive afterwards.
$abi = @(Select-String -LiteralPath (Join-Path $coreDir 'api.h') -Pattern 'vt_core_[a-z_]+(?=\s*\()' -AllMatches |
        ForEach-Object { $_.Matches } | ForEach-Object { $_.Value } | Sort-Object -Unique)
if ($abi.Count -lt 19) { throw "Expected at least 19 C ABI exports in api.h, found $($abi.Count)." }

$optimization = if ($Configuration -eq 'Release') { '-O2' } else { '-O0' }
$produced = [Collections.Generic.List[string]]::new()
foreach ($source in $sources) {
    $object = Join-Path $objects (($source -replace '/', '_') + '.o')
    # -fwasm-exceptions matches the SDK's runtime link (WasmEnableExceptionHandling).
    # The default -fexceptions (Emscripten SJLJ) emits __cxa_find_matching_catch_*
    # helpers that only Emscripten's own link wires up.
    & $emcc $optimization -std=c++20 -fwasm-exceptions -fno-rtti -I $coreDir -c (Join-Path $coreDir "$source.cpp") -o $object
    if ($LASTEXITCODE -ne 0) { throw "emcc failed for $source.cpp." }
    $produced.Add($object)
}

if (Test-Path -LiteralPath $result) { Remove-Item -LiteralPath $result -Force }
& $llvmAr rcs $result @produced
if ($LASTEXITCODE -ne 0) { throw 'Failed to archive the C++ core.' }

# A plain archive keeps each member whole, so every entry point stays available.
$nm = Join-Path $toolchain 'bin/llvm-nm.exe'
if (!(Test-Path -LiteralPath $nm)) { $nm = Join-Path $toolchain 'bin/llvm-nm' }
$symbols = & $nm --defined-only $result
if ($LASTEXITCODE -ne 0) { throw 'Failed to inspect the native core archive.' }
foreach ($entry in $abi) {
    if (!($symbols -match " [TtWw] $entry$")) { throw "Native core archive is missing $entry." }
}
Write-Host "Native C++ core for WASM built at $result"
