# MeetRec - builds the portable, ffmpeg-bundled Windows x64 release package.
#
# Usage:
#   powershell -File scripts\package-release.ps1 -Version 1.1.0
#   powershell -File scripts\package-release.ps1 -FfmpegDir D:\ffmpeg\bin
#
# The application discovers ffmpeg.exe next to MeetRec.exe, so
# the whole folder is the install: unzip = install. The package includes the
# ffmpeg GPLv3 license and a source pointer for the exact build.

param(
    [string]$Configuration = "Release",
    [string]$Version = "1.1.0",
    [string]$FfmpegDir = "C:\ffmpeg\bin",
    [string]$OutputRoot = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $root "artifacts\dist"
}
$app = Join-Path $root "artifacts\$Configuration\app\MeetRec.exe"
if (-not (Test-Path $app)) {
    throw "Missing application binary: $app. Build with -Configuration $Configuration first."
}
$ffmpeg = Join-Path $FfmpegDir "ffmpeg.exe"
if (-not (Test-Path $ffmpeg)) {
    throw "Missing ffmpeg.exe at $ffmpeg. Pass -FfmpegDir <dir>."
}
$license = Join-Path (Split-Path -Parent $FfmpegDir) "LICENSE.txt"
if (-not (Test-Path $license)) {
    throw "Missing ffmpeg LICENSE.txt at $license."
}

$name = "MeetRec-$Version-win64-with-ffmpeg"
$stage = Join-Path $OutputRoot $name
$zip = Join-Path $OutputRoot "$name.zip"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
if (Test-Path $zip) { Remove-Item -Force $zip }
New-Item -ItemType Directory -Force -Path (Join-Path $stage "licenses") | Out-Null

Copy-Item $app (Join-Path $stage "MeetRec.exe")
foreach ($pattern in @("ffmpeg.exe", "*.dll")) {
    Get-ChildItem $FfmpegDir -Filter $pattern -File | ForEach-Object {
        Copy-Item $_.FullName (Join-Path $stage $_.Name)
    }
}
Copy-Item $license (Join-Path $stage "licenses\FFmpeg-GPLv3.txt")

$readme = Join-Path $root "docs\release\README-portable.md"
$notes = Join-Path $root "docs\release\RELEASE-NOTES-$Version.md"
if (-not (Test-Path $readme)) { throw "Missing $readme" }
if (-not (Test-Path $notes)) { throw "Missing $notes" }
Copy-Item $readme (Join-Path $stage "README.txt")
Copy-Item $notes (Join-Path $stage "RELEASE-NOTES.txt")

$source = @"
FFmpeg source and license
=========================

This package bundles an unmodified FFmpeg build:
  ffmpeg version N-121437-gf4a87d8ca4-20251015 (GPLv3 build)
  commit: f4a87d8ca4
  configuration: --enable-gpl --enable-version3 --enable-libx264 (shared DLL build)

License: GNU General Public License v3 (see licenses/FFmpeg-GPLv3.txt).
Source code for this exact FFmpeg revision:
  https://github.com/FFmpeg/FFmpeg/tree/f4a87d8ca4

MeetRec runs ffmpeg.exe as a separate child process (pipe transport); it does
not link FFmpeg libraries into the application binary.
"@
Set-Content -Path (Join-Path $stage "licenses\FFmpeg-SOURCE.txt") -Value $source -Encoding UTF8

# Windows' bsdtar produces a normal deflate zip without the high memory use of
# Compress-Archive on a 200+ MB payload.
Push-Location $stage
try {
    # Use the Windows bsdtar explicitly; Git Bash may put GNU tar on PATH,
    # which treats "C:\..." as a remote host.
    $tar = Join-Path $env:SystemRoot "System32\tar.exe"
    & $tar -a -c -f $zip *
    if ($LASTEXITCODE -ne 0) { throw "tar.exe failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash
$sums = Join-Path $OutputRoot "SHA256SUMS.txt"
"$hash  $(Split-Path -Leaf $zip)" | Set-Content -Path $sums -Encoding ASCII

Write-Host "Package: $zip"
Write-Host "Size:    $([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB"
Write-Host "SHA256:  $hash"
Write-Host "Folder:  $stage"
