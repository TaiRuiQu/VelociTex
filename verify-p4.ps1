param([switch]$RegenerateReference,[switch]$SkipRasterDiagnostics)
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    function Run-Node([string[]]$Arguments){& node @Arguments;if($LASTEXITCODE -ne 0){throw "Verification failed: node $($Arguments -join ' ')"}}
    Run-Node @('tools/reference/generate-core-symbols.mjs','--check')
    Run-Node @('tools/reference/generate-layout-data.mjs','--check')
    Run-Node @('tools/reference/generate-unicode-data.mjs','--check')
    Run-Node @('tools/reference/generate-command-data.mjs','--check')
    Run-Node @('tools/reference/generate-fallback-metrics.mjs','--check')
    Run-Node @('tools/reference/generate-p4-corpus.mjs','--registrations','--edges','--edges2','--check')
    if($RegenerateReference){Run-Node @('tools/reference/p1-reference.mjs','--phase=p4','--full-matrix')}
    Run-Node @('tools/reference/p1-compare.mjs','--phase=p4')
    Run-Node @('tools/reference/p1-runtime-parity.mjs','--phase=p4')
    # User confirmed on2026-10-19: anti-aliasing differences do not block acceptance.
    # Mathematical hard differences above and any clipping below still fail.
    if(!$SkipRasterDiagnostics){Run-Node @('tools/reference/p1-images.mjs','--phase=p4','--layout-only')}
    Run-Node @('tools/reference/p4-api-audit.mjs')
}
finally{Pop-Location}
