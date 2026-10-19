# Run the actual workflow's pre-publish gate against valid and tampered local archives.
# This script never extracts or invokes the npm publish command.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
Push-Location $repoPath
$keys = @('PACKAGE_VERSION', 'WEB_VERSION', 'DIST_TAG', 'PACKAGE_FILE', 'PACKAGE_SHA256', 'SOURCE_REVISION')
$previous = @{}
foreach ($key in $keys) { $previous[$key] = [Environment]::GetEnvironmentVariable($key) }
try {
    & node --test tests/wasm/release.test.mjs
    if ($LASTEXITCODE -ne 0) { throw 'npm release tests failed.' }
    $workflow = Get-Content .github/workflows/publish.yml -Raw
    $section = ($workflow -split '      - name: Verify the tested tarball before publishing', 2)[1]
    $block = (($section -split '        run: \|\r?\n', 2)[1] -split '\r?\n      - name:', 2)[0]
    if (!$block) { throw 'Unable to find the real workflow publish gate.' }
    $script = [scriptblock]::Create(($block -replace '(?m)^          ', ''))
    $fixture = Get-Content artifacts/wasm/release-tests/last-fixture.txt -Raw
    $scratch = Join-Path $repoPath ('artifacts/npm-ci/gate-' + [guid]::NewGuid().ToString('N'))
    $releasePath = Join-Path $scratch 'npm-release'
    New-Item -ItemType Directory -Force $releasePath | Out-Null
    Copy-Item (Join-Path $fixture '*.tgz') $releasePath
    Copy-Item (Join-Path $fixture 'npm-release.json') $releasePath
    $reportPath = Join-Path $releasePath 'npm-release.json'
    $original = Get-Content $reportPath -Raw
    $report = $original | ConvertFrom-Json
    $env:PACKAGE_VERSION = $report.version; $env:WEB_VERSION = $report.version
    $env:DIST_TAG = $report.distTag; $env:PACKAGE_FILE = $report.filename
    $env:PACKAGE_SHA256 = $report.sha256; $env:SOURCE_REVISION = $report.sourceRevision
    Push-Location $scratch
    try {
        & $script
        foreach ($mutation in @('dirty-source', 'hash-mismatch', 'version-mismatch', 'wrong-channel')) {
            $record = $original | ConvertFrom-Json
            switch ($mutation) {
                'dirty-source' { $record.sourceHasChanges = $true }
                'hash-mismatch' { $record.sha256 = '0' * 64; $env:PACKAGE_SHA256 = $record.sha256 }
                'version-mismatch' { $env:WEB_VERSION = '9.9.9' }
                'wrong-channel' { $env:DIST_TAG = 'latest' }
            }
            $record | ConvertTo-Json | Set-Content $reportPath -Encoding utf8NoBOM
            $rejected = $false
            try { & $script } catch { $rejected = $true }
            if (!$rejected) { throw "Publish gate accepted $mutation" }
            $env:PACKAGE_SHA256 = $report.sha256; $env:WEB_VERSION = $report.version; $env:DIST_TAG = $report.distTag
        }
        $original | Set-Content $reportPath -Encoding utf8NoBOM
        & $script
        Write-Host 'Actual workflow publish gate: valid archive passed; dirty source, changed hash, mismatched version and wrong channel rejected.'
    }
    finally { Pop-Location }
}
finally {
    foreach ($key in $keys) { [Environment]::SetEnvironmentVariable($key, $previous[$key]) }
    Pop-Location
}
