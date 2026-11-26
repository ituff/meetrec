# MeetRec - builds the portable, ffmpeg-bundled Windows x64 release package.
#
# Usage:
#   powershell -File scripts\package-release.ps1 -Version 1.5.3
#   powershell -File scripts\package-release.ps1 -FfmpegDir D:\ffmpeg\bin
# The application discovers ffmpeg.exe next to MeetRec.exe, so
# the whole folder is the install: unzip = install. The package includes the
# ffmpeg GPLv3 license and a source pointer for the exact build.

param(
    [string]$Configuration = "Release",
    [string]$Version = "1.5.3",
    [string]$FfmpegDir = "C:\ffmpeg\bin",
    [string]$FfmpegLicense = "",
    [string]$FfmpegSourceUrl = "https://github.com/FFmpeg/FFmpeg",
    [string]$OutputRoot = "",
    # Optional directory holding model.int8.onnx / tokens.txt / silero_vad.onnx.
    # When supplied, a separate MeetRec-<ver>-win64-speech-model.zip is built.
    [string]$SpeechModelDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $root "artifacts\dist"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputRoot)) {
    $OutputRoot = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $OutputRoot))
}
$app = Join-Path $root "artifacts\$Configuration\app\MeetRec.exe"
if (-not (Test-Path $app)) {
    throw "Missing application binary: $app. Build with -Configuration $Configuration first."
}
$ffmpeg = Join-Path $FfmpegDir "ffmpeg.exe"
if (-not (Test-Path $ffmpeg)) {
    throw "Missing ffmpeg.exe at $ffmpeg. Pass -FfmpegDir <dir>."
}
# The compressor probes old recordings with ffprobe before transcoding.
$ffprobe = Join-Path $FfmpegDir "ffprobe.exe"
if (-not (Test-Path $ffprobe)) {
    throw "Missing ffprobe.exe at $ffprobe (same folder as ffmpeg.exe)."
}
if ([string]::IsNullOrWhiteSpace($FfmpegLicense)) {
    $FfmpegLicense = Join-Path (Split-Path -Parent $FfmpegDir) "LICENSE.txt"
}
$license = $FfmpegLicense
if (-not (Test-Path $license)) {
    throw "Missing ffmpeg license file at $license. Pass -FfmpegLicense <file>."
}
$ffmpegVersion = (& $ffmpeg -version 2>&1 | Select-Object -First 1)
$ffmpegVersion = ($ffmpegVersion -join ' ').Trim()
if ([string]::IsNullOrWhiteSpace($ffmpegVersion)) {
    throw "Could not query ffmpeg version from $ffmpeg."
}

$name = "MeetRec-$Version-win64-with-ffmpeg"
$stage = Join-Path $OutputRoot $name
$zip = Join-Path $OutputRoot "$name.zip"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
if (Test-Path $zip) { Remove-Item -Force $zip }
New-Item -ItemType Directory -Force -Path (Join-Path $stage "licenses") | Out-Null

Copy-Item $app (Join-Path $stage "MeetRec.exe")
foreach ($pattern in @("ffmpeg.exe", "ffprobe.exe", "*.dll")) {
    Get-ChildItem $FfmpegDir -Filter $pattern -File | ForEach-Object {
        Copy-Item $_.FullName (Join-Path $stage $_.Name)
    }
}
Copy-Item $license (Join-Path $stage "licenses\FFmpeg-GPLv3.txt")

# Local meeting transcription runtime (SenseVoiceSmall int8 via sherpa-onnx).
# The DLLs are delay-loaded, so an installation that loses them still records.
$sherpaBin = Join-Path $root "artifacts\third_party\sherpa-onnx\bin"
foreach ($dll in @("sherpa-onnx-c-api.dll", "onnxruntime.dll",
                   "onnxruntime_providers_shared.dll")) {
    $source = Join-Path $sherpaBin $dll
    if (-not (Test-Path $source)) {
        throw "Missing $source. Run scripts\fetch-sherpa-onnx.ps1 and rebuild."
    }
    Copy-Item $source (Join-Path $stage $dll)
}
$sherpaLicense = Join-Path $root "docs\release\licenses\sherpa-onnx-Apache-2.0.txt"
if (-not (Test-Path $sherpaLicense)) {
    throw "Missing $sherpaLicense"
}
Copy-Item $sherpaLicense (Join-Path $stage "licenses\sherpa-onnx-Apache-2.0.txt")
$sherpaNotice = @"
sherpa-onnx runtime
===================

Bundled: sherpa-onnx 2.0.3 (win-x64, shared, /MT)
License: Apache-2.0 (see licenses/sherpa-onnx-Apache-2.0.txt)
Source:  https://github.com/k2-fsa/sherpa-onnx

MeetRec delay-loads sherpa-onnx-c-api.dll for local meeting transcription.
The speech model itself is distributed separately
(MeetRec-<version>-win64-speech-model.zip) and is not part of this package.
Removing the DLLs disables transcription; recording is unaffected.
"@
[System.IO.File]::WriteAllText(
    (Join-Path $stage "licenses\sherpa-onnx-NOTICE.txt"),
    $sherpaNotice,
    (New-Object System.Text.UTF8Encoding($false)))

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
  $ffmpegVersion

License: GNU General Public License v3 (see licenses/FFmpeg-GPLv3.txt).
Source / build provider:
  $FfmpegSourceUrl

MeetRec runs ffmpeg.exe and ffprobe.exe as separate child processes (pipe and
probe transport); it does not link FFmpeg libraries into the application
binary.
"@
[System.IO.File]::WriteAllText(
    (Join-Path $stage "licenses\FFmpeg-SOURCE.txt"),
    $source,
    (New-Object System.Text.UTF8Encoding($false)))

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

# Optional speech model pack: separate archive, unzipped over the application
# folder (it contains models/sensevoice/...).
$modelZip = $null
$modelHash = $null
if (-not [string]::IsNullOrWhiteSpace($SpeechModelDir)) {
    $modelDir = (Resolve-Path -LiteralPath $SpeechModelDir).Path
    foreach ($required in @("model.int8.onnx", "tokens.txt", "silero_vad.onnx")) {
        if (-not (Test-Path (Join-Path $modelDir $required))) {
            throw "Speech model directory is missing $required`: $modelDir"
        }
    }
    $modelName = "MeetRec-$Version-win64-speech-model"
    $modelStage = Join-Path $OutputRoot $modelName
    $modelZip = Join-Path $OutputRoot "$modelName.zip"
    if (Test-Path $modelStage) { Remove-Item -Recurse -Force $modelStage }
    if (Test-Path $modelZip) { Remove-Item -Force $modelZip }
    $modelTarget = Join-Path $modelStage "models\sensevoice"
    $modelLicenses = Join-Path $modelStage "licenses"
    New-Item -ItemType Directory -Force -Path $modelTarget, $modelLicenses | Out-Null
    foreach ($file in @("model.int8.onnx", "tokens.txt", "silero_vad.onnx")) {
        Copy-Item (Join-Path $modelDir $file) $modelTarget -Force
    }
    $funasrLicense = Join-Path $root "docs\release\licenses\FunASR-MODEL-LICENSE.txt"
    if (-not (Test-Path $funasrLicense)) { throw "Missing $funasrLicense" }
    Copy-Item $funasrLicense (Join-Path $modelLicenses "FunASR-MODEL-LICENSE.txt")
    $modelNotice = @"
Meeting transcription model pack
================================

Extract this archive into the MeetRec application folder so the files land in
models\sensevoice\ next to MeetRec.exe:

  models\sensevoice\model.int8.onnx
  models\sensevoice\tokens.txt
  models\sensevoice\silero_vad.onnx

Model: SenseVoiceSmall int8 (sherpa-onnx export)
Source: https://github.com/k2-fsa/sherpa-onnx/releases/tag/asr-models
License: FunASR Model Open Source License Agreement v1.1
         (see licenses/FunASR-MODEL-LICENSE.txt)
         源模型：https://github.com/FunAudioLLM/SenseVoice

Recognition runs locally; audio is never uploaded.
"@
    [System.IO.File]::WriteAllText(
        (Join-Path $modelStage "README.txt"),
        $modelNotice,
        (New-Object System.Text.UTF8Encoding($false)))
    Push-Location $modelStage
    try {
        & $tar -a -c -f $modelZip *
        if ($LASTEXITCODE -ne 0) { throw "tar.exe failed with exit code $LASTEXITCODE" }
    } finally {
        Pop-Location
    }
    $modelHash = (Get-FileHash $modelZip -Algorithm SHA256).Hash
}

$sums = Join-Path $OutputRoot "SHA256SUMS.txt"
$sumLines = @("$hash  $(Split-Path -Leaf $zip)")
if ($modelZip -ne $null) {
    $sumLines += "$modelHash  $(Split-Path -Leaf $modelZip)"
}
$sumLines | Set-Content -Path $sums -Encoding ASCII

Write-Host "Package: $zip"
Write-Host "Size:    $([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB"
Write-Host "SHA256:  $hash"
if ($modelZip -ne $null) {
    Write-Host "Speech model pack: $modelZip"
    Write-Host "Speech model size: $([math]::Round((Get-Item $modelZip).Length / 1MB, 1)) MB"
    Write-Host "Speech model SHA256: $modelHash"
}
Write-Host "Folder:  $stage"
