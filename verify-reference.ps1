param([switch]$FullMatrix)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
        & $Executable @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$Executable failed: $($Arguments -join ' ')" }
    }
    Invoke-Checked npm.cmd @('ci', '--prefix', 'tools/reference', '--ignore-scripts')
    Invoke-Checked node @('tools/reference/acquire.mjs')
    Invoke-Checked node @('tools/reference/inventory.mjs')
    Invoke-Checked node @('tools/reference/check-source-dist.mjs')
    Invoke-Checked node @('tools/reference/check-normal-corpus.mjs')
    Invoke-Checked python @('tools/VelociTex.DataGeneration/generate_fonts.py', '--upstream', 'artifacts/upstream/npm/package', '--check')
    $reference = @('tools/reference/reference.mjs')
    if ($FullMatrix) { $reference += '--full-matrix' }
    Invoke-Checked node $reference
    Invoke-Checked node @('tools/reference/font-calibration.mjs')
    Invoke-Checked python @('tools/VelociTex.DataGeneration/verify_browser_fonts.py')
    Invoke-Checked node @('tools/reference/inspect-fonts.mjs')
    Invoke-Checked node @('tools/reference/check-baseline.mjs')
    Invoke-Checked node @('tools/reference/check-evidence.mjs')
}
finally { Pop-Location }
