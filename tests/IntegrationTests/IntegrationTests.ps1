# Virtual Screen Recorder - end-to-end integration harness (T082).
#
# Requires: application built. The driver-dependent steps run only when the
# driver responds to --self-check; otherwise they are reported as SKIP so the
# harness stays honest on machines without the WDK payload staged.
#
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

if (-not (Test-Path $app))      { Write-Error "[VSR-IT-001] App not found: $app (build first)"; exit 1 }
if (-not (Test-Path $validator)) { Write-Error "[VSR-IT-002] MediaValidator not found: $validator"; exit 1 }

$clipDir = Join-Path $resultsDir "clips"
New-Item -ItemType Directory -Force -Path $clipDir | Out-Null

$report = [ordered]@{ startedUtc = (Get-Date).ToUniversalTime().ToString("o"); steps = @() }
$failed = $false

function Add-Report {
    param([hashtable]$Step)
    $script:report.steps += [ordered]@{
        name = $Step.name; result = $Step.result; detail = $Step.detail
    }
    Write-Host ("{0,-8} {1}: {2}" -f $Step.result, $Step.name, $Step.detail)
}

# Runs the app under a hard timeout. Known AMD MFT per-session stalls can hang
# the sink writer indefinitely; the harness must degrade to a TIMEOUT FAIL and
# move on instead of wedging the whole run.
function Invoke-AppWithTimeout {
    param([string]$Arguments, [int]$TimeoutSec = 150)
    $outFile = Join-Path $env:TEMP ("vsr-harness-" + [guid]::NewGuid().ToString("N") + ".out")
    # Run through cmd so the app's exit code propagates reliably to cmd's own
    # exit code; PS 5.1 Start-Process can return a null ExitCode.
    $cmd = '/c ""' + $app + '" ' + $Arguments + ' > "' + $outFile + '" 2>&1& exit %ERRORLEVEL%"'
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = "$env:windir\System32\cmd.exe"
    $psi.Arguments = $cmd
    $psi.UseShellExecute = $false
    $p = [System.Diagnostics.Process]::Start($psi)
    if (-not $p.WaitForExit($TimeoutSec * 1000)) {
        & "$env:windir\System32\taskkill.exe" /F /T /PID $p.Id | Out-Null
        $p.WaitForExit() | Out-Null
        return @{ exit = -999; output = @("TIMEOUT: app did not exit within $TimeoutSec s (known MFT stall); process tree killed") }
    }
    $output = @()
    if (Test-Path $outFile) { $output = (Get-Content $outFile | Select-Object -Last 4) }
    Remove-Item $outFile -Force -ErrorAction SilentlyContinue
    return @{ exit = $p.ExitCode; output = $output }
}

# ---- Step 1: unit tests ------------------------------------------------------
& (Join-Path $PSScriptRoot "..\..\scripts\test.ps1") -Configuration $Configuration
if ($LASTEXITCODE -eq 0) {
    Add-Report @{ name = "unit-tests"; result = "PASS"; detail = "scripts\test.ps1 exit 0" }
} else {
    Add-Report @{ name = "unit-tests"; result = "FAIL"; detail = "scripts\test.ps1 exit $LASTEXITCODE" }
    $failed = $true
}

# ---- Step 2: capability self-check -------------------------------------------
$selfCheck = & $app --self-check 2>&1
$selfCheckExit = $LASTEXITCODE
$driverReady = ($selfCheckExit -eq 0) -and (($selfCheck -join "`n") -match "monitor active")
$selfCheckResult = "PASS"
if ($selfCheckExit -ne 0) { $selfCheckResult = "FAIL" }
Add-Report @{
    name   = "self-check"
    result = $selfCheckResult
    detail = "exit $selfCheckExit"
}

# ---- Step 2b: encoder pipeline validation (no driver needed, T031) -----------
$encodeExpect = @{
    "low"      = @{ w = 1280; h = 720;  fps = 30 };
    "standard" = @{ w = 1920; h = 1080; fps = 30 };
    "high"     = @{ w = 1920; h = 1080; fps = 60 };
}
foreach ($preset in @("low", "standard", "high")) {
    Write-Host "== Encoder pipeline $preset for $ClipSeconds s =="
    $e = $encodeExpect[$preset]
    $r = Invoke-AppWithTimeout "--encode-test --seconds $ClipSeconds --preset $preset --output `"$clipDir`""
    $exit = $r.exit
    $out = $r.output
    if ($exit -ne 0) {
        Add-Report @{ name = "encode-$preset"; result = "FAIL"
                      detail = "encode-test exit $exit :: $($out -join ' | ')" }
        $failed = $true
        continue
    }
    $clip = Get-ChildItem -Path $clipDir -Filter "*.mp4" |
        Where-Object { $_.Name -notmatch '\.partial\.mp4$' } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    & $validator $clip.FullName --expect-width $e.w --expect-height $e.h --expect-fps $e.fps `
        --expect-duration-sec $ClipSeconds --tolerance-sec 2 `
        --expect-frames ($e.fps * $ClipSeconds) | Out-Null
    $vExit = $LASTEXITCODE
    if ($vExit -eq 0) {
        Add-Report @{ name = "encode-$preset"; result = "PASS"; detail = $clip.Name }
    } else {
        Add-Report @{ name = "encode-$preset"; result = "FAIL"
                      detail = "validator exit $vExit ($($clip.Name))" }
        $failed = $true
    }
}

# ---- Step 2c: driverless window recording (no driver needed) ----------------
# Records a self-contained animated test window through the real capture ->
# encode pipeline. The virtual display and its driver are not involved. The
# synthetic source's presentation rate is environment-bound (an RDP
# composition cadence caps it well below the preset), so frame rate is not
# asserted; a frame-count floor proves frames actually flowed.
$expect = @{
    "low"      = @{ w = 1280; h = 720;  fps = 30 };
    "standard" = @{ w = 1920; h = 1080; fps = 30 };
    "high"     = @{ w = 1920; h = 1080; fps = 60 };
}
foreach ($preset in @("low", "standard", "high")) {
    Write-Host "== Recording window $preset for $ClipSeconds s =="
    $r = Invoke-AppWithTimeout "--dev-record-window --seconds $ClipSeconds --preset $preset --output `"$clipDir`""
    $exit = $r.exit
    $out = $r.output
    if ($exit -ne 0) {
        Add-Report @{ name = "record-window-$preset"; result = "FAIL"
                      detail = "dev-record-window exit $exit :: $($out -join ' | ')" }
        $failed = $true
        continue
    }
    $clip = Get-ChildItem -Path $clipDir -Filter "*.mp4" |
        Where-Object { $_.Name -notmatch '\.partial\.mp4$' } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $e = $expect[$preset]
    $vout = & $validator $clip.FullName --expect-width $e.w --expect-height $e.h `
        --expect-duration-sec $ClipSeconds --tolerance-sec 3 2>&1
    $vExit = $LASTEXITCODE
    $decoded = 0
    if ("$vout" -match "decoded frames: (\d+)") { $decoded = [int]$Matches[1] }
    $floor = 5 * $ClipSeconds
    if ($vExit -eq 0 -and $decoded -ge $floor) {
        Add-Report @{ name = "record-window-$preset"; result = "PASS"
                      detail = "$($clip.Name): decoded=$decoded" }
    } else {
        Add-Report @{ name = "record-window-$preset"; result = "FAIL"
                      detail = "validator exit $vExit decoded=$decoded floor=$floor ($($clip.Name))" }
        $failed = $true
    }
}

foreach ($preset in @("low", "standard", "high")) {
    if (-not $driverReady) {
        Add-Report @{ name = "record-$preset"; result = "SKIP"
                      detail = "virtual display not active (driver not staged on this machine)" }
        continue
    }

    Write-Host "== Recording $preset for $ClipSeconds s =="
    $r = Invoke-AppWithTimeout "--dev-record --seconds $ClipSeconds --preset $preset --output `"$clipDir`""
    $exit = $r.exit
    $out = $r.output

    if ($exit -ne 0) {
        Add-Report @{ name = "record-$preset"; result = "FAIL"
                      detail = "dev-record exit $exit :: $($out -join ' | ')" }
        $failed = $true
        continue
    }

    # Locate the newest mp4 for this run.
    $clip = Get-ChildItem -Path $clipDir -Filter "*.mp4" |
        Where-Object { $_.Name -notmatch '\.partial\.mp4$' } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $clip) {
        Add-Report @{ name = "record-$preset"; result = "FAIL"; detail = "no output file produced" }
        $failed = $true
        continue
    }

    $e = $expect[$preset]
    & $validator $clip.FullName `
        --expect-width $e.w --expect-height $e.h --expect-fps $e.fps `
        --expect-duration-sec $ClipSeconds --tolerance-sec 2 `
        --expect-frames ($e.fps * $ClipSeconds) | Out-Null
    $vExit = $LASTEXITCODE
    $detail = "$($clip.Name): $([string]::Join('; ', ($out | Select-Object -Last 2)))"
    if ($vExit -eq 0) {
        Add-Report @{ name = "record-$preset"; result = "PASS"; detail = $detail }
    } else {
        Add-Report @{ name = "record-$preset"; result = "FAIL"; detail = "validator exit $vExit" }
        $failed = $true
    }
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
