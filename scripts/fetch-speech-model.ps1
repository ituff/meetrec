# MeetRec - fetch the SenseVoiceSmall int8 model pack for local meeting
# transcription. The pack is ~230 MB and is distributed separately from the
# application zip; the app also accepts a manually seeded models/sensevoice
# directory.
#
# Usage:
#   powershell -File scripts\fetch-speech-model.ps1 [-Destination D:\Models\sensevoice] [-Force]
#
# IMPORTANT: this is the sherpa-onnx export. The FunASR/funasr_onnx export has
# no sherpa-onnx metadata and a different tokens.txt format, so it cannot be
# substituted here.

param(
    [string]$Destination = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $root "models\sensevoice"
}
$base = Join-Path $root "artifacts\third_party\speech-model"
$archiveName = "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17.tar.bz2"
$modelSha = "7d1efa2138a65b0b488df37f8b89e3d91a60676e416f515b952358d83dfd347e"
$vadSha = "9e2449e1087496d8d4caba907f23e0bd3f78d91fa552479bb9c23ac09cbb1fd6"
$archive = Join-Path $base "dl\$archiveName"
$vadFile = Join-Path $base "dl\silero_vad.onnx"
$extracted = Join-Path $base "extracted\sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17"
$marker = Join-Path $Destination "version.txt"
$markerText = "sherpa-onnx-sense-voice-int8-2024-07-17 $modelSha $vadSha"

if (-not $Force -and (Test-Path (Join-Path $Destination "model.int8.onnx")) -and
    (Test-Path (Join-Path $Destination "tokens.txt")) -and
    (Test-Path (Join-Path $Destination "silero_vad.onnx")) -and
    (Test-Path $marker) -and ((Get-Content $marker -Raw).Trim() -eq $markerText)) {
    Write-Host "Speech model pack is already available at $Destination."
    exit 0
}

New-Item -ItemType Directory -Force -Path (Join-Path $base "dl"), (Join-Path $base "extracted"),
    $Destination | Out-Null

$curl = Join-Path $env:SystemRoot "System32\curl.exe"
if (-not (Test-Path $curl)) {
    $curl = (Get-Command curl.exe -ErrorAction Stop).Source
}

function Get-Asset([string]$Url, [string]$Path, [string]$ExpectedSha, [int]$MaxTimeSec = 3600) {
    Write-Host "Downloading $Url"
    # A hanging mirror must not consume the whole CI job timeout.
    & $curl -L --retry 5 --retry-delay 3 -C - --connect-timeout 20 --max-time $MaxTimeSec -sS -o $Path $Url
    if ($LASTEXITCODE -ne 0) {
        throw "curl failed with exit code $LASTEXITCODE for $Url"
    }
    if (-not (Test-Path $Path)) { throw "Download produced no file: $Path" }
    $hash = (Get-FileHash -Path $Path -Algorithm SHA256).Hash
    if ($hash -ne $ExpectedSha) {
        Remove-Item -Force $Path -ErrorAction SilentlyContinue
        throw "SHA256 mismatch for $Path`n  expected $ExpectedSha`n  actual   $hash"
    }
}

function Get-WithFallback([string]$GithubUrl, [string]$Path, [string]$ExpectedSha) {
    if ($Force -and (Test-Path $Path)) { Remove-Item -Force $Path }
    if ((Test-Path $Path) -and -not $Force) {
        $hash = (Get-FileHash -Path $Path -Algorithm SHA256).Hash
        if ($hash -eq $ExpectedSha) { return }
    }
    $proxy = "https://jscdn.msone.top/$GithubUrl"
    try {
        # The mirror can be slow; cap it so a stalled transfer falls back.
        Get-Asset $proxy $Path $ExpectedSha 600
        return
    } catch {
        Write-Warning "Proxy download failed: $($_.Exception.Message)"
        Write-Host "Retrying directly from github.com..."
    }
    Get-Asset $GithubUrl $Path $ExpectedSha
}

$modelUrl = "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/$archiveName"
$vadUrl = "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/silero_vad.onnx"
Get-WithFallback $modelUrl $archive $modelSha
Get-WithFallback $vadUrl $vadFile $vadSha

# A cached or interrupted extraction can leave read-only/locked files behind;
# bsdtar may then wait on an overwrite prompt. Always extract into a clean tree.
$extractRoot = Join-Path $base "extracted"
if (Test-Path $extractRoot) { Remove-Item -Recurse -Force $extractRoot }
New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
$tar = Join-Path $env:SystemRoot "System32\tar.exe"
Write-Host "Extracting $archiveName"
& $tar -xjf $archive -C $extractRoot
if ($LASTEXITCODE -ne 0) { throw "tar.exe failed with exit code $LASTEXITCODE" }
if (-not (Test-Path (Join-Path $extracted "model.int8.onnx"))) {
    throw "Expected model.int8.onnx missing under $extracted"
}

Copy-Item (Join-Path $extracted "model.int8.onnx") $Destination -Force
Copy-Item (Join-Path $extracted "tokens.txt") $Destination -Force
Copy-Item $vadFile $Destination -Force
if (Test-Path (Join-Path $extracted "LICENSE")) {
    Copy-Item (Join-Path $extracted "LICENSE") (Join-Path $Destination "MODEL-LICENSE.txt") -Force
}
$funasrLicense = Join-Path $root "docs\release\licenses\FunASR-MODEL-LICENSE.txt"
if (Test-Path $funasrLicense) {
    Copy-Item $funasrLicense (Join-Path $Destination "MODEL-LICENSE-FunASR.txt") -Force
}
Set-Content -Path $marker -Value $markerText -Encoding ASCII

$sizeMb = [math]::Round((Get-Item (Join-Path $Destination "model.int8.onnx")).Length / 1MB, 1)
Write-Host "Speech model pack ready at $Destination (model $sizeMb MB)"
