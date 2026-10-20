# Check code formatting
[CmdletBinding()]
param(
    [switch]$Fix
)
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    $hasErrors = $false

    # Check C# formatting using dotnet format
    Write-Host 'Checking C# formatting...'
    $formatArgs = @('format', 'VelociTex.slnx', '--verify-no-changes')
    if ($Fix) {
        $formatArgs = @('format', 'VelociTex.slnx')
    }
    & dotnet @formatArgs
    if ($LASTEXITCODE -ne 0) {
        $hasErrors = $true
        if (!$Fix) {
            Write-Host 'C# formatting issues found. Run with -Fix to auto-format.' -ForegroundColor Red
        }
    } else {
        Write-Host 'C# formatting OK' -ForegroundColor Green
    }

    # Check JavaScript/JSON formatting with Prettier if available
    $prettier = Get-Command npx -ErrorAction SilentlyContinue
    if ($prettier) {
        Write-Host 'Checking JavaScript/JSON formatting...'
        $prettierArgs = @('--no-install', 'prettier')
        if ($Fix) {
            $prettierArgs += @('--write', '**/*.{js,mjs,json}')
        } else {
            $prettierArgs += @('--check', '**/*.{js,mjs,json}')
        }
        & npx @prettierArgs
        if ($LASTEXITCODE -ne 0) {
            $hasErrors = $true
            if (!$Fix) {
                Write-Host 'JavaScript/JSON formatting issues found. Run with -Fix to auto-format.' -ForegroundColor Red
            }
        } else {
            Write-Host 'JavaScript/JSON formatting OK' -ForegroundColor Green
        }
    } else {
        Write-Host 'Prettier not available, skipping JavaScript/JSON format check' -ForegroundColor Yellow
    }

    # Check C++ formatting with clang-format if available
    $clangFormat = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($clangFormat) {
        Write-Host 'Checking C++ formatting...'
        $cppFiles = @(Get-ChildItem -Path native/core -Include *.cpp,*.hpp,*.h -Recurse -File | Where-Object { $_.Name -notlike '*.inc' -and $_.DirectoryName -notlike '*\migrate*' })
        foreach ($file in $cppFiles) {
            if ($Fix) {
                & clang-format -i $file.FullName
            } else {
                $formatted = & clang-format $file.FullName
                $original = Get-Content $file.FullName -Raw
                if ($formatted -ne $original) {
                    Write-Host "Formatting issue in $($file.FullName)" -ForegroundColor Red
                    $hasErrors = $true
                }
            }
        }
        if (!$hasErrors -or $Fix) {
            Write-Host 'C++ formatting OK' -ForegroundColor Green
        }
    } else {
        Write-Host 'clang-format not available, skipping C++ format check' -ForegroundColor Yellow
    }

    if ($hasErrors -and !$Fix) {
        throw 'Code formatting check failed. Run ./tools/Test-Format.ps1 -Fix to auto-format.'
    }
    Write-Host 'All formatting checks passed!' -ForegroundColor Green
}
finally { Pop-Location }
