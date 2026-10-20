param(
    [string]$Out = "artifacts/wasm/fastmath"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$sdk = "C:\Program Files\dotnet\packs\Microsoft.NET.Runtime.Emscripten.3.1.56.Sdk.win-x64\10.0.13\tools"
$python = "C:\Program Files\dotnet\packs\Microsoft.NET.Runtime.Emscripten.3.1.56.Python.win-x64\10.0.13\tools"
$node = "C:\Program Files\dotnet\packs\Microsoft.NET.Runtime.Emscripten.3.1.56.Node.win-x64\10.0.13\tools\bin"
$env:PATH = "$python;$node;$sdk\emscripten;$sdk\bin;$env:PATH"
$env:DOTNET_EMSCRIPTEN_LLVM_ROOT = "$sdk\bin"
$env:DOTNET_EMSCRIPTEN_NODE_JS = "$node\node.exe"
$env:DOTNET_EMSCRIPTEN_BINARYEN_ROOT = "$sdk"
$env:FROZEN_CACHE = "1"
$env:EM_CACHE = "C:\Program Files\dotnet\packs\Microsoft.NET.Runtime.Emscripten.3.1.56.Cache.win-x64\10.0.13\tools\emscripten\cache"
$outDir = Join-Path $root $Out
New-Item -ItemType Directory -Force $outDir | Out-Null
$source = Join-Path $PSScriptRoot "VelociTex.NativeMath.cpp"
$output = Join-Path $outDir "fastmath.js"
$arguments = @(
    "-O3", "-std=c++20", "-fno-exceptions", "-fno-rtti", "--no-entry",
    "-sWASM=1", "-sMODULARIZE=1", "-sEXPORT_ES6=1", "-sEXPORT_NAME=createFastMath",
    "-sENVIRONMENT=web", "-sFILESYSTEM=0", "-sALLOW_MEMORY_GROWTH=1",
    "-sINITIAL_MEMORY=16777216", "-sSTACK_SIZE=1048576", "-sASSERTIONS=0",
    "-sEXPORTED_FUNCTIONS=_vt_mathml2,_vt_mathml,_vt_mathml_supported,_malloc,_free",
    "-sEXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString,stringToUTF8,lengthBytesUTF8",
    "-o", $output, $source
)
& "$sdk\emscripten\emcc.bat" @arguments
if ($LASTEXITCODE -ne 0) { throw "emcc failed with exit code $LASTEXITCODE" }
Get-ChildItem $outDir | Select-Object Name, Length
