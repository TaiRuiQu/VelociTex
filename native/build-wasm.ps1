param(
    [string]$Out = "artifacts/wasm/fastmath"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$onWindows = $env:OS -eq 'Windows_NT'
$separator = if ($onWindows) { ';' } else { ':' }

$dotnetRoot = if ($env:DOTNET_ROOT) { $env:DOTNET_ROOT } else { Split-Path (Get-Command dotnet).Source }
$packs = Join-Path $dotnetRoot 'packs'
function Find-Pack([string]$pattern) {
    $pack = Get-ChildItem $packs -Directory -Filter $pattern -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if (!$pack) { return $null }
    $version = Get-ChildItem $pack.FullName -Directory |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (!$version) { return $null }
    return Join-Path $version.FullName 'tools'
}

$sdk = Find-Pack 'Microsoft.NET.Runtime.Emscripten.*.Sdk.*'
if (!$sdk) { throw 'The .NET wasm-tools Emscripten SDK pack was not found.' }
$python = Find-Pack 'Microsoft.NET.Runtime.Emscripten.*.Python.*'
$node = Find-Pack 'Microsoft.NET.Runtime.Emscripten.*.Node.*'
$cache = Find-Pack 'Microsoft.NET.Runtime.Emscripten.*.Cache.*'

$pathParts = @()
if ($python) { $pathParts += $python }
if ($node) { $pathParts += (Join-Path $node 'bin') }
$nodeCommand = Get-Command node -ErrorAction SilentlyContinue
if (!$nodeCommand -and !$node) { throw 'Node.js is required for the Emscripten build.' }
$nodePath = if ($nodeCommand) { $nodeCommand.Source } elseif ($onWindows) { Join-Path $node 'bin/node.exe' } else { Join-Path $node 'bin/node' }

$pathParts += (Join-Path $sdk 'emscripten')
$pathParts += (Join-Path $sdk 'bin')
$env:PATH = ($pathParts -join $separator) + $separator + $env:PATH
$env:DOTNET_EMSCRIPTEN_LLVM_ROOT = Join-Path $sdk 'bin'
$env:DOTNET_EMSCRIPTEN_NODE_JS = $nodePath
$env:DOTNET_EMSCRIPTEN_BINARYEN_ROOT = $sdk
if ($cache) {
    $env:FROZEN_CACHE = '1'
    $env:EM_CACHE = Join-Path $cache 'emscripten/cache'
} else {
    $env:FROZEN_CACHE = ''
    $env:EM_CACHE = Join-Path $root 'artifacts/wasm/emcache'
}

$outDir = Join-Path $root $Out
New-Item -ItemType Directory -Force $outDir | Out-Null
$source = Join-Path $PSScriptRoot 'VelociTex.NativeMath.cpp'
$output = Join-Path $outDir 'fastmath.js'
$arguments = @(
    '-O3', '-std=c++20', '-fno-exceptions', '-fno-rtti', '--no-entry',
    '-sWASM=1', '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sEXPORT_NAME=createFastMath',
    '-sENVIRONMENT=web', '-sFILESYSTEM=0', '-sALLOW_MEMORY_GROWTH=1',
    '-sINITIAL_MEMORY=16777216', '-sSTACK_SIZE=1048576', '-sASSERTIONS=0',
    '-sEXPORTED_FUNCTIONS=_vt_mathml2,_vt_mathml,_vt_mathml_supported,_malloc,_free',
    '-sEXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString,stringToUTF8,lengthBytesUTF8',
    '-o', $output, $source
)
$emccName = if ($onWindows) { 'emcc.bat' } else { 'emcc' }
$emcc = Join-Path $sdk "emscripten/$emccName"
& $emcc @arguments
if ($LASTEXITCODE -ne 0) { throw "emcc failed with exit code $LASTEXITCODE" }
Get-ChildItem $outDir | Select-Object Name, Length

