# Formatting gates for the repository.
#
# C#            dotnet format (whitespace, style and analyzer fixes) for both
#               solutions, driven by .editorconfig.
# JS/JSON/TS    Prettier from the pinned root package-lock.json (npm ci).
# C++           clang-format from the pinned clang-format Python package, using
#               .clang-format.
#
# CI runs this without -Fix; any drift fails the build. Run it with -Fix before
# committing to apply the canonical style.
[CmdletBinding()]
param(
    [switch]$Fix,
    # Format only the files changed against this git revision (or the working
    # tree when unset). dotnet format still analyzes whole projects because its
    # analyzers need the full compilation.
    [string]$ChangedFrom
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Push-Location $root
try {
    $solutions = @('VelociTex.slnx', 'VelociTex.Web.slnx')
    $changed = $null
    if ($ChangedFrom) {
        $changed = @(& git diff --name-only --diff-filter=ACMR $ChangedFrom --)
        if ($LASTEXITCODE -ne 0) { throw "Unable to diff against '$ChangedFrom'." }
        $changed = @($changed | ForEach-Object { $_ -replace '\\', '/' })
        if ($changed.Count -eq 0) {
            Write-Host 'No changed files to format.' -ForegroundColor Green
            return
        }
    }

    function Test-Changed([string]$Path) {
        return !$changed -or $changed -contains $Path
    }
    function Invoke-Tool([string]$Name, [string[]]$Arguments) {
        & $Name @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$Name failed with exit code $LASTEXITCODE." }
    }

    $failures = [Collections.Generic.List[string]]::new()

    # C#: dotnet format resolves its own SDK from global.json. Sources under
    # Generated/ are emitted and byte-verified by the locked generators, so they
    # must never be rewritten by the formatter.
    $generated = @('**/Generated/**')
    foreach ($solution in $solutions) {
        Write-Host "Checking C# formatting: $solution"
        $arguments = @('format', $solution, '--no-restore', '--verbosity', 'minimal')
        foreach ($pattern in $generated) { $arguments += @('--exclude', $pattern) }
        if (!$Fix) { $arguments += '--verify-no-changes' }
        & dotnet @arguments
        if ($LASTEXITCODE -ne 0) {
            if ($Fix) { throw "dotnet format failed for $solution." }
            $failures.Add("C# formatting differs in $solution")
        }
    }


    # JavaScript, TypeScript and JSON: pinned Prettier with .prettierignore.
    Write-Host 'Checking JavaScript/TypeScript/JSON formatting'
    $prettier = Join-Path $root 'node_modules/.bin/prettier.cmd'
    if (!(Test-Path -LiteralPath $prettier)) { $prettier = Join-Path $root 'node_modules/.bin/prettier' }
    if (!(Test-Path -LiteralPath $prettier)) {
        throw 'Prettier is not installed. Run: npm ci'
    }
    $targets = @('.')
    if ($changed) {
        $formattable = @('js', 'mjs', 'mts', 'ts', 'json', 'md', 'yml', 'yaml', 'html', 'css')
        $targets = @($changed | Where-Object { $formattable -contains ([IO.Path]::GetExtension($_).TrimStart('.').ToLowerInvariant()) })
        # package-lock.json and generated data are excluded by .prettierignore.
        $targets = @($targets | Where-Object { $_ -notmatch '(^|/)(package-lock\.json|[^/]*\.lock\.json)$' -and $_ -notmatch '(^|/)Generated/' })
    }
    if ($targets.Count -gt 0) {
        & $prettier '--log-level' 'warn' $(if ($Fix) { '--write' } else { '--check' }) @targets
        if ($LASTEXITCODE -ne 0) {
            if ($Fix) { throw 'Prettier failed.' }
            $failures.Add('JavaScript/TypeScript/JSON formatting differs (run Prettier)')
        }
    }

    # C++: clang-format with the pinned package version. Sources generated into
    # native/core/generated are byte-verified by generate_native_data.py and
    # generate_native_svg.py, and .inc fragments are physically split from a
    # translation unit and are not standalone C++ files.
    Write-Host 'Checking C++ formatting'
    $clangFormat = Get-Command clang-format -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (!$clangFormat) {
        $python = Get-Command python -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($python) {
            $modulePath = & $python.Source -c 'import os, clang_format; print(os.path.dirname(clang_format.__file__))' 2>$null
            if ($LASTEXITCODE -eq 0 -and $modulePath) {
                $candidate = Join-Path $modulePath 'data/bin/clang-format.exe'
                if (Test-Path -LiteralPath $candidate) {
                    $clangFormat = Get-Item -LiteralPath $candidate
                }
            }
        }
    }
    if (!$clangFormat) {
        throw 'clang-format is not installed. Run: python -m pip install -r tools/requirements-format.txt'
    }
    Write-Host "Using $($clangFormat.Source)"
    $nativeFiles = @(& git ls-files --cached --others --exclude-standard -- '*.cpp' '*.hpp' '*.h' '*.cc' '*.c')
    if ($LASTEXITCODE -ne 0) { throw 'Unable to enumerate native sources.' }
    $nativeFiles = @($nativeFiles | ForEach-Object { $_ -replace '\\', '/' } | Where-Object {
            (Test-Path -LiteralPath $_) -and $_ -notmatch '(^|/)(migrate|generated)/' -and (Test-Changed $_)
        })
    foreach ($file in $nativeFiles) {
        $arguments = @('--style=file', '--fallback-style=LLVM')
        if ($Fix) {
            & $clangFormat.Source @arguments -i $file
            if ($LASTEXITCODE -ne 0) { throw "clang-format failed: $file" }
            continue
        }
        $expected = & $clangFormat.Source @arguments $file
        if ($LASTEXITCODE -ne 0) { throw "clang-format failed: $file" }
        $actual = [IO.File]::ReadAllText((Join-Path $root $file)) -replace "`r`n", "`n"
        if ((($expected -join "`n").TrimEnd() + "`n") -ne ($actual.TrimEnd() + "`n")) {
            $failures.Add("C++ formatting differs: $file")
        }
    }

    if ($failures.Count > 0) {
        $failures | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
        throw 'Code formatting check failed. Run ./tools/Test-Format.ps1 -Fix to auto-format.'
    }
    Write-Host 'All formatting checks passed.' -ForegroundColor Green
}
finally { Pop-Location }
