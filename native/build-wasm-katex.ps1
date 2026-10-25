# Compile the C++ core into a standalone WebAssembly module for the npm package.
#
# The browser loads this module directly: it exposes the same KaTeX-compatible
# ABI the JavaScript adapter expects (`vt_alloc`, `vt_render`, ...), and the
# KaTeX-identical HTML/CSS and MathML come from the very same C++ core the
# .NET endpoint uses.  No JavaScript or Rust formula parser is involved.
#
# The module has no JavaScript imports and no Emscripten glue: the host
# allocates a UTF-8 buffer, calls `vt_render` and reads the returned bytes.
param(
    [string]$Out = "artifacts/wasm/core",
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$onWindows = $env:OS -eq 'Windows_NT'
$separator = if ($onWindows) { ';' } else { ':' }

$dotnetRoot = if ($env:DOTNET_ROOT) { $env:DOTNET_ROOT } else { Split-Path (Get-Command dotnet).Source }
$packs = Join-Path $dotnetRoot 'packs'
function Find-Pack([string]$pattern) {
    Get-ChildItem $packs -Directory -Filter $pattern -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1 |
        ForEach-Object {
            Get-ChildItem $_.FullName -Directory |
                Sort-Object { [version]$_.Name } -Descending |
                Select-Object -First 1 |
                ForEach-Object { Join-Path $_.FullName 'tools' }
        }
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
# The cache pack of the .NET wasm-tools workload is frozen for the ordinary
# runtime build, which does not use link time optimisation; it therefore holds
# only the non-LTO system libraries.  -flto asks for the LTO variants
# (`sysroot/lib/wasm32-emscripten/lto`), which the frozen pack does not carry,
# so the build falls back to a writable cache and lets Emscripten build them
# once.
$frozenCache = if ($cache) { Join-Path $cache 'emscripten/cache' } else { $null }
$ltoCache = if ($frozenCache) { Join-Path $frozenCache 'sysroot/lib/wasm32-emscripten/lto/crt1_reactor.o' } else { $null }
if ($ltoCache -and (Test-Path -LiteralPath $ltoCache)) {
    $env:FROZEN_CACHE = '1'
    $env:EM_CACHE = $frozenCache
} else {
    # `bool(os.getenv('FROZEN_CACHE', 'True'))` of the SDK config treats any
    # non-empty value as true, and Windows does not keep an empty environment
    # variable, so the frozen cache is turned off in a config of our own.
    $env:EM_CACHE = Join-Path $root 'artifacts/wasm/emcache'
    $configDir = Join-Path $root 'artifacts/wasm'
    New-Item -ItemType Directory -Force $configDir | Out-Null
    $configPath = Join-Path $configDir 'emscripten-config.py'
    $originalConfig = (Join-Path $sdk 'emscripten/.emscripten').Replace('\', '\\')
    @(
        "exec(compile(open(r'$originalConfig', 'rb').read(), r'$originalConfig', 'exec'))",
        'FROZEN_CACHE = False'
    ) | Set-Content -LiteralPath $configPath -Encoding Ascii
    $env:EM_CONFIG = $configPath
}

$outDir = Join-Path $root $Out
New-Item -ItemType Directory -Force $outDir | Out-Null
$output = Join-Path $outDir 'velocitex-core.wasm'

# Every translation unit of the core plus the standalone entry points.  The
# order mirrors native/CMakeLists.txt so that both builds stay in sync.
$coreDir = Join-Path $PSScriptRoot 'core'
$sources = @(
    'types', 'lexer', 'macros', 'parser', 'cache', 'layout', 'geometry', 'mathml', 'svg', 'core',
    'generated/data', 'generated/svg_data',
    'html/domtree', 'html/options', 'html/build_common', 'html/build_html', 'html/sqrt_image',
    'html/delimiter', 'html/accent', 'html/array', 'html/enclose',
    'generated/html_paths', 'generated/html_sqrt_paths', 'pool_alloc'
)
$files = @((Join-Path $PSScriptRoot 'wasm/katex_wasm.cpp'))
foreach ($source in $sources) {
    $files += (Join-Path $coreDir "$source.cpp")
}

$optimization = if ($Configuration -eq 'Release') { @('-O3', '-flto') } else { @('-O0') }
# `-mnontrapping-fptoint` turns the conversion of a double into an integer
# into a single instruction.  Emscripten switches the feature off by default,
# and the conversion is then a comparison with a branch into the trap handler.
# The feature needs a browser of 2021 or later (Chrome 74, Firefox 62,
# Safari 15); the package falls back to the .NET runtime when the module
# cannot be instantiated.
# The linear memory is fixed: the module is loaded once and the heap of the
# core is a bump arena that is released at the start of every render, so the
# module never needs to grow.  A fixed memory keeps the buffer of the host
# valid for the whole life of the module, and it is what the published
# artifact was measured with.
#
# `-mbulk-memory` would turn a copy of a run time length into the
# `memory.copy` instruction instead of a call into the C library.  It was
# measured again on the current core with a clean single flag A/B and it is
# still not visible: the Node paired measurement reads +1.5% in one order and
# -0.3% in the other (the null control of the same window reads +0.46%), and
# the browser gate cannot resolve it.  The flag would also add a feature
# requirement to the published module, so it stays off.
# The profile of the core tells the compiler which paths of the renderer are
# the hot ones, so that it lays them out next to each other instead of
# spreading them over the whole text segment.  The profile is collected by
# tools/reference/generate-pgo-profile.mjs from the acceptance corpus of the
# repository (1605 real world formulas), and it is a build input: refresh it
# when the core changes.  The profile is read by every compiler of LLVM 13 or
# later; a compiler whose IR does not match the profile warns that the
# profile is out of date and lays the code out without it.
$profile = Join-Path $PSScriptRoot 'wasm/katex.pgo'
$profileArguments = if (Test-Path -LiteralPath $profile) { @("-fprofile-use=$profile") } else { @() }

# The list has to stay flat: `Start-Process` binds `-ArgumentList` to a
# `string[]`, and a nested array cannot be converted to a string.
$arguments = $optimization + @(
    '-std=c++20', '-fwasm-exceptions', '-mnontrapping-fptoint', '-fno-rtti', '--no-entry',
    '-sWASM=1', '-sENVIRONMENT=web', '-sFILESYSTEM=0', '-sASSERTIONS=0',
    '-sEXPORTED_FUNCTIONS=_vt_alloc,_vt_free,_vt_render,_vt_result_len,_vt_free_result,_vt_version',
    '-sEXPORTED_RUNTIME_METHODS=HEAPU8',
    '-I', $coreDir,
    '-o', $output
) + $profileArguments + $files

$emccName = if ($onWindows) { 'emcc.bat' } else { 'emcc' }
$emcc = Join-Path $sdk "emscripten/$emccName"
$stderrPath = Join-Path $outDir 'emcc.stderr.txt'
# Windows PowerShell 5.1 turns native stderr into a terminating error under
# $ErrorActionPreference=Stop; capture it and fail only on the real exit code.
$process = Start-Process -FilePath $emcc -ArgumentList $arguments -NoNewWindow -Wait -PassThru -RedirectStandardError $stderrPath
if ($process.ExitCode -ne 0) {
    if (Test-Path -LiteralPath $stderrPath) { Get-Content -LiteralPath $stderrPath | Write-Host }
    throw "emcc failed with exit code $($process.ExitCode)"
}
if (!(Test-Path -LiteralPath $output)) { throw "emcc did not produce $output" }
Get-ChildItem $outDir | Select-Object Name, Length
