# setup-vcpkg-overlay.ps1
#
# Clone the stdware/vcpkg-overlay repository (latest main) into scripts/vcpkg
# for local development. The vcpkg overlay provides custom ports and triplets
# referenced by scripts/vcpkg-manifest/vcpkg.json (overlay-ports / overlay-triplets).
#
# This script is idempotent: if scripts/vcpkg is already a non-empty directory,
# it prints a hint and exits 0 without re-cloning.
#
# CI uses an equivalent inline step in .github/workflows/build.yml.
#
# Usage:
#   pwsh scripts/setup-vcpkg-overlay.ps1
#   pwsh scripts/setup-vcpkg-overlay.ps1 -Force   # discard existing and re-clone

[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$targetDir = Join-Path $repoRoot 'scripts\vcpkg'
$overlayUrl = 'https://github.com/stdware/vcpkg-overlay.git'

if ((Test-Path $targetDir) -and (Get-ChildItem -Path $targetDir -Force -ErrorAction SilentlyContinue)) {
    if ($Force) {
        Write-Host "setup-vcpkg-overlay: -Force specified, removing existing $targetDir"
        Remove-Item -Recurse -Force $targetDir
    } else {
        Write-Host "setup-vcpkg-overlay: $targetDir already exists and is non-empty."
        Write-Host "  To refresh, delete it and re-run, or run with -Force."
        exit 0
    }
}

Write-Host "setup-vcpkg-overlay: cloning $overlayUrl into $targetDir"
git clone --depth 1 $overlayUrl $targetDir
if ($LASTEXITCODE -ne 0) {
    Write-Error "setup-vcpkg-overlay: git clone failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}

Write-Host "setup-vcpkg-overlay: clone complete."
Write-Host "  Overlay ports:   $targetDir\ports"
Write-Host "  Overlay triplets: $targetDir\triplets"
