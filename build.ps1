<#
.SYNOPSIS
    Windows shortcut: configures, builds and tests Sonolint with a single command.

.DESCRIPTION
    Runs "cmake --workflow --preset windows". The only thing it adds is finding the
    CMake bundled with Visual Studio when cmake is not on the PATH.

.EXAMPLE
    .\build.ps1
#>
$ErrorActionPreference = 'Stop'

function Find-CMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    # CMake bundled with Visual Studio / Build Tools
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $installs = & $vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        foreach ($vs in $installs) {
            $candidate = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path $candidate) { return $candidate }
        }
    }

    Write-Host 'ERROR: CMake not found. Install CMake, or the "C++ CMake tools for Windows" component in the Visual Studio Installer.' -ForegroundColor Red
    exit 1
}

$cmake = Find-CMake

Push-Location $PSScriptRoot
try {
    & $cmake --workflow --preset windows
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
