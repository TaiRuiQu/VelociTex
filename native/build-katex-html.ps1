# Build the KaTeX-compatible HTML/CSS WebAssembly module.
#
# The committed prebuilt module is used when no Rust toolchain is available so
# that consumers can still build the npm package. CI builds from source to
# verify the vendored crate.
[CmdletBinding()]
param(
    [string]$Out = 'artifacts/wasm/katex-html',
    [switch]$RequireSourceBuild
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$crate = Join-Path $PSScriptRoot 'katex-html'
$prebuilt = Join-Path $crate 'prebuilt/vt-katex-html.wasm'
$outDir = if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $root $Out }
$target = Join-Path $crate 'target/wasm32-unknown-unknown/release/vt_katex_html.wasm'
$cargo = Get-Command cargo -ErrorAction SilentlyContinue
$built = $false
if ($cargo) {
    $rustup = Get-Command rustup -ErrorAction SilentlyContinue
    if ($rustup) {
        try {
            & $rustup.Source target add wasm32-unknown-unknown 2>&1 | Out-Null
        }
        catch {
            Write-Warning "Unable to add the wasm32-unknown-unknown Rust target: $($_.Exception.Message)"
        }
    }
    try {
        Push-Location $crate
        try {
            & $cargo.Source build --release --target wasm32-unknown-unknown --locked
            if ($LASTEXITCODE -ne 0) { throw "cargo build exited with $LASTEXITCODE" }
        }
        finally { Pop-Location }
        if (!(Test-Path -LiteralPath $target)) { throw "cargo did not produce $target" }
        $built = $true
    }
    catch {
        if ($RequireSourceBuild) { throw }
        Write-Warning "Rust source build unavailable ($($_.Exception.Message)); using the committed prebuilt module."
    }
}
elseif ($RequireSourceBuild) {
    throw 'cargo is required for -RequireSourceBuild.'
}
if (!$built) {
    if (!(Test-Path -LiteralPath $prebuilt)) { throw 'No Rust toolchain and no committed prebuilt KaTeX HTML module.' }
    $target = $prebuilt
}
New-Item -ItemType Directory -Force $outDir | Out-Null
Copy-Item -LiteralPath $target -Destination (Join-Path $outDir 'vt-katex-html.wasm') -Force
# A caught cargo failure must not leave a stale non-zero code for the caller;
# the prebuilt fallback is a successful build outcome.
$global:LASTEXITCODE = 0
Write-Host "KaTeX-compatible HTML WASM built at $(Join-Path $outDir 'vt-katex-html.wasm')"
