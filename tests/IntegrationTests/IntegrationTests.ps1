# Virtual Screen Recorder - end-to-end integration harness (T082).
#
# Requires: driver package staged (scripts\install-driver.ps1), application built.
# Produces artifacts\test-results\integration-report.json.

param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [int]$ClipSeconds = 10
)

$ErrorActionPreference = "Continue"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$app = Join-Path $root "artifacts\$Configuration\app\VirtualScreenRecorder.exe"
$validator = Join-Path $root "artifacts\$Configuration\tests\MediaValidator.exe"
$resultsDir = Join-Path $root "artifacts\test-results"
New-Item -ItemType Directory -Force -Path $resultsDir | Out-Null

if (-not (Test-Path $app))     { Write-Error "[VSR-IT-001] App not found: $app (build first)"; exit 1 }
if (-not (Test-Path $validator)) { Write-Error "[VSR-IT-002] MediaValidator not found: $validator"; exit 1 }

$clipDir = Join-Path $resultsDir "clips"
New-Item -ItemType Directory -Force -Path $clipDir | Out-Null

$expect = @{
    "low"      = @{ w = 1280; h = 1080; fps = 30 }; # placeholder corrected below
    "standard" = @{ w = 1920; h = 1080; fps = 30 };
    "high"     = @{ w = 1920; h = 1080; fps = 60 };
}
$expect["low"] = @{ w = 1280; h = 720; fps = 30 }

$report = [ordered]@{ startedUtc = (Get-Date).ToUniversalTime().ToString("o"); clips = @() }
$failed = $false

foreach ($preset in @("low", "standard", "high")) {
    Write-Host "== Recording $preset for $ClipSeconds s =="
    $out = & $app --dev-record --seconds $ClipSeconds --preset $preset --output $clipDir 2>&1
    $exit = $LASTEXITCODE
    $line = [ordered]@{ preset = $preset; devRecordExit = $exit; output = ($out -join " | ") }

    if ($exit -ne 0) {
        $line["result"] = "FAIL(dev-record)"
        $failed = $true
    } else {
        # Locate the newest mp4 for this run.
        $clip = Get-ChildItem -Path $clipDir -Filter "*.mp4" |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (-not $clip) {
            $line["result"] = "FAIL(no output file)"
            $failed = $true
        } else {
            $e = $expect[$preset]
            & $validator $clip.FullName `
                --expect-width $e.w --expect-height $e.h --expect-fps $e.fps `
                --expect-duration-sec $ClipSeconds --tolerance-sec 2
            $vExit = $LASTEXITCODE
            $line["clip"] = $clip.Name
            $line["validatorExit"] = $vExit
            $line["result"] = $vExit -eq 0 ? "PASS" : "FAIL(validator)"
            if ($vExit -ne 0) { $failed = $true }
        }
    }
    $report.clips += $line
}

$report.finishedUtc = (Get-Date).ToUniversalTime().ToString("o")
$report.failed = $failed
$report | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $resultsDir "integration-report.json") -Encoding UTF8

if ($failed) {
    Write-Host "Integration harness FAILED. Report: artifacts\test-results\integration-report.json" -ForegroundColor Red
    exit 1
}
Write-Host "Integration harness PASSED. Report: artifacts\test-results\integration-report.json" -ForegroundColor Green
exit 0
