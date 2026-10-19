param([switch]$RegenerateReference)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    function Run-Node([string[]]$Arguments) {
        & node @Arguments
        if ($LASTEXITCODE -ne 0) { throw "Verification failed: node $($Arguments -join ' ')" }
    }
    Run-Node @('tools/reference/generate-core-symbols.mjs', '--check')
    Run-Node @('tools/reference/generate-layout-data.mjs', '--check')
    Run-Node @('tools/reference/generate-p2-corpus.mjs', '--check')
    if ($RegenerateReference) { Run-Node @('tools/reference/p1-reference.mjs', '--phase=p2', '--full-matrix') }
    Run-Node @('tools/reference/p1-compare.mjs', '--phase=p2')
    Run-Node @('tools/reference/p1-images.mjs', '--phase=p2', '--reviewed')
    Run-Node @('tools/reference/p1-runtime-parity.mjs', '--phase=p2')
    Run-Node @('tools/reference/p1-coverage.mjs', '--phase=p2')
}
finally { Pop-Location }
