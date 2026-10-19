param([switch]$RegenerateReference)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    function Invoke-Node([string[]]$Arguments) {
        & node @Arguments
        if ($LASTEXITCODE -ne 0) { throw "P1 check failed: $($Arguments -join ' ')" }
    }
    Invoke-Node @('tools/reference/generate-core-symbols.mjs','--check')
    Invoke-Node @('tools/reference/generate-layout-data.mjs','--check')
    if ($RegenerateReference) { Invoke-Node @('tools/reference/p1-reference.mjs','--full-matrix') }
    Invoke-Node @('tools/reference/p1-compare.mjs')
    Invoke-Node @('tools/reference/p1-images.mjs','--reviewed')
    Invoke-Node @('tools/reference/p1-runtime-parity.mjs')
    Invoke-Node @('tools/reference/p1-coverage.mjs')
    Write-Host 'P1 hard checks passed. Raw raster failures and exact-hash reviewed differences remain visible in image-check.json; changed or unreviewed images fail.'
} finally { Pop-Location }
