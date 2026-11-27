# MeetRec - fetch the pinned sherpa-onnx runtime used by the local meeting
# transcript (SenseVoiceSmall int8). The package is a third-party release asset,
# not a checked-in binary; it is cached under artifacts/third_party.
#
# Usage:
#   powershell -File scripts\fetch-sherpa-onnx.ps1 [-Force]
#
# The runtime is MIT/Apache-2.0 (sherpa-onnx) and is delay-loaded by MeetRec,
# so a deployment without it still records normally.

param(
    [string]$Version = "2.0.3",
    [string]$ExpectedSha256 = "1d2414ac8921f718ba3188782f80475f525a3d82eeb59cef75ceb9360fbf8182",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$base = Join-Path $root "artifacts\third_party\sherpa-onnx"
$asset = "sherpa-onnx-v$Version-win-x64-shared-MT-Release.tar.bz2"
$download = Join-Path $base "dl\$asset"
$extractedRoot = Join-Path $base "extracted"
$packageDir = Join-Path $extractedRoot "sherpa-onnx-v$Version-win-x64-shared-MT-Release"
$header = Join-Path $base "include\sherpa-onnx\c-api\c-api.h"
$marker = Join-Path $base "version.txt"
$markerText = "$Version $ExpectedSha256"

if (-not $Force -and (Test-Path $header) -and (Test-Path $marker) -and
    ((Get-Content $marker -Raw).Trim() -eq $markerText)) {
    Write-Host "sherpa-onnx $Version is already available."
    exit 0
}

New-Item -ItemType Directory -Force -Path (Join-Path $base "dl"), $extractedRoot,
    (Join-Path $base "include"), (Join-Path $base "lib"), (Join-Path $base "bin") |
    Out-Null

$curl = Join-Path $env:SystemRoot "System32\curl.exe"
if (-not (Test-Path $curl)) {
    $curl = (Get-Command curl.exe -ErrorAction Stop).Source
}
$githubUrl = "https://github.com/k2-fsa/sherpa-onnx/releases/download/v$Version/$asset"
# Same China-accessible asset proxy the updater uses; github.com remains the
# fallback and the SHA256 is pinned here, not in the proxy.
$proxyUrl = "https://jscdn.msone.top/$githubUrl"

function Get-Asset([string]$Url, [string]$Path, [int]$MaxTimeSec = 1800) {
    Write-Host "Downloading $Url"
    # A hanging mirror must not consume the whole CI job timeout.
    & $curl -L --retry 3 --retry-delay 2 -C - --connect-timeout 20 --max-time $MaxTimeSec -sS -o $Path $Url
    if ($LASTEXITCODE -ne 0) {
        throw "curl failed with exit code $LASTEXITCODE for $Url"
    }
    if (-not (Test-Path $Path)) { throw "Download produced no file: $Path" }
    $hash = (Get-FileHash -Path $Path -Algorithm SHA256).Hash
    if ($hash -ne $ExpectedSha256) {
        Remove-Item -Force $Path -ErrorAction SilentlyContinue
        throw "SHA256 mismatch for $Path`n  expected $ExpectedSha256`n  actual   $hash"
    }
}

if ($Force -and (Test-Path $download)) { Remove-Item -Force $download }
$downloadOk = $false
if ((Test-Path $download) -and -not $Force) {
    $hash = (Get-FileHash -Path $download -Algorithm SHA256).Hash
    $downloadOk = $hash -eq $ExpectedSha256
}
if (-not $downloadOk) {
    try {
        # The China-friendly mirror can be slow or stale; cap the attempt and
        # fall back to github.com.
        Get-Asset $proxyUrl $download 600
        $downloadOk = $true
    } catch {
        Write-Warning "Proxy download failed: $($_.Exception.Message)"
        Write-Host "Retrying directly from github.com..."
    }
}
if (-not $downloadOk) {
    Get-Asset $githubUrl $download
    $downloadOk = $true
}
if (-not $downloadOk) { throw "Could not obtain $asset" }

# A cached or interrupted extraction can leave read-only/locked files behind;
# bsdtar may then wait on an overwrite prompt. Always extract into a clean tree.
if (Test-Path $extractedRoot) { Remove-Item -Recurse -Force $extractedRoot }
New-Item -ItemType Directory -Force -Path $extractedRoot | Out-Null
$tar = Join-Path $env:SystemRoot "System32\tar.exe"
Write-Host "Extracting $asset"
& $tar -xjf $download -C $extractedRoot
if ($LASTEXITCODE -ne 0) { throw "tar.exe failed with exit code $LASTEXITCODE" }
if (-not (Test-Path $packageDir)) {
    throw "Expected extracted folder missing: $packageDir"
}

Remove-Item -Recurse -Force (Join-Path $base "include"), (Join-Path $base "lib"),
    (Join-Path $base "bin") -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path (Join-Path $base "include"),
    (Join-Path $base "lib"), (Join-Path $base "bin") | Out-Null

Copy-Item (Join-Path $packageDir "include\*") (Join-Path $base "include") -Recurse -Force
Copy-Item (Join-Path $packageDir "lib\sherpa-onnx-c-api.lib") (Join-Path $base "lib") -Force
Copy-Item (Join-Path $packageDir "lib\onnxruntime.lib") (Join-Path $base "lib") -Force
Copy-Item (Join-Path $packageDir "lib\sherpa-onnx-c-api.dll") (Join-Path $base "bin") -Force
Copy-Item (Join-Path $packageDir "lib\onnxruntime.dll") (Join-Path $base "bin") -Force
Copy-Item (Join-Path $packageDir "lib\onnxruntime_providers_shared.dll") (Join-Path $base "bin") -Force

Set-Content -Path $marker -Value $markerText -Encoding ASCII
Write-Host "sherpa-onnx $Version ready at $base"
