# package-win.ps1 - build a portable, self-contained Windows release of wled-pc-rgb.
#
# The C++ build already produces a standalone folder (exe + WledBackend.java + the
# full MinGW/Qt DLL closure) via core-cpp/scripts/deploy-win.sh. This script stages
# that folder together with the docs and zips it into dist/wled-pc-rgb-vX.Y-win64.zip.
#
# The result runs on any Windows 64-bit PC that has the two runtime prerequisites the
# app orchestrates but does not bundle: a JDK (for WledBackend.java) and OpenRGB. See
# docs/USAGE.md - those are listed there and surfaced in-app by the setup dots.
#
#   powershell -File scripts/package-win.ps1 [-Version 0.19] [-SkipBuild]

param(
    [string]$Version = "0.19",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$app  = Join-Path $repo "core-cpp\build\app"
$dist = Join-Path $repo "dist"

if (-not $SkipBuild) {
    Write-Host "==> Building release (MSYS2 UCRT64)..."
    $env:MSYSTEM = "UCRT64"
    $bash = "C:\.software\scoop\apps\msys2\current\usr\bin\bash.exe"
    & $bash -lc "cd /c/.code/wled-pc-rgb/core-cpp && cmake --build build -j"
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}

if (-not (Test-Path (Join-Path $app "wled_pc_rgb.exe"))) {
    throw "No build at $app - build first (or omit -SkipBuild)."
}

$stageName = "wled-pc-rgb-v$Version-win64"
$stage = Join-Path $dist $stageName
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

# Copy the self-contained app, minus CMake bookkeeping.
Write-Host "==> Staging app files..."
Get-ChildItem $app -File | Where-Object { $_.Name -ne "cmake_install.cmake" } |
    ForEach-Object { Copy-Item $_.FullName -Destination $stage }

# Docs alongside the binary.
foreach ($doc in @("README.md", "docs\USAGE.md", "docs\ROADMAP.md", "docs\DESIGN.md", "LICENSE")) {
    $p = Join-Path $repo $doc
    if (Test-Path $p) { Copy-Item $p -Destination $stage }
}

$zip = Join-Path $dist "$stageName.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Write-Host "==> Zipping to $zip"
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -CompressionLevel Optimal

$mb = "{0:N1}" -f ((Get-Item $zip).Length / 1MB)
$n  = (Get-ChildItem $stage -File).Count
Write-Host "==> Done: $stageName.zip  ($n files, $mb MB)"
