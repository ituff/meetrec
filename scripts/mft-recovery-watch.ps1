# MFT recovery watchdog (development machine tool).
#
# Polls the encoder with a short --encode-test probe every 10 minutes until
# the AMD MFT session state recovers (pollution was caused by force-killed
# wedged sessions). On recovery it runs the full evidence chain:
#   1. Release 5-minute 1080p60 soak + MediaValidator validation
#   2. Release 2-hour  1080p30 soak + MediaValidator validation (NFR-007/AC-008)
# All output is appended to %TEMP%\vsr-mft-watch.log so the session that
# started this script can exit freely.
#
# Launch (detached):
#   powershell -NoProfile -Command "Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','scripts\mft-recovery-watch.ps1'"

$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
$app = Join-Path $root "artifacts\Release\app\MeetRec.exe"
$validator = Join-Path $root "artifacts\Release\tests\MediaValidator.exe"
$log = Join-Path $env:TEMP "vsr-mft-watch.log"
$clipDir = Join-Path $env:TEMP "vsr-mft-watch-clips"

function Log {
    param([string]$msg)
    Add-Content -Path $log -Value ("[" + (Get-Date).ToString("HH:mm:ss") + "] " + $msg)
}

Log "watchdog started"

# Phase 1: probe every 10 minutes, up to 3 hours.
$recovered = $false
for ($probe = 1; $probe -le 18 -and -not $recovered; $probe++) {
    Start-Sleep -Seconds 600
    $p = Start-Process -FilePath $app `
        -ArgumentList "--encode-test","--seconds","20","--preset","standard","--output",$clipDir `
        -PassThru -WindowStyle Hidden -Wait
    if ($p.ExitCode -eq 0) {
        $recovered = $true
        Log "probe $probe RECOVERED (exit 0)"
    } else {
        Log "probe $probe still stalling (exit $($p.ExitCode))"
    }
}
if (-not $recovered) {
    Log "no recovery within the polling window; reboot required before re-running soak evidence"
    exit 2
}

# Phase 2: 5-minute high soak + validation.
$soak1 = Start-Process -FilePath $app `
    -ArgumentList "--encode-test","--seconds","300","--preset","high","--output",$clipDir `
    -PassThru -WindowStyle Hidden -Wait
Log "soak-5min-high exit=$($soak1.ExitCode)"
$clip = Get-ChildItem -Path $clipDir -Filter "*.mp4" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$v = Start-Process -FilePath $validator `
    -ArgumentList $clip.FullName,"--expect-width","1920","--expect-height","1080","--expect-fps","60","--expect-duration-sec","300","--tolerance-sec","2","--expect-frames","18000" `
    -PassThru -WindowStyle Hidden -Wait
Log "soak-5min-high validator exit=$($v.ExitCode)"

# Phase 3: 2-hour standard soak + validation (NFR-007 / AC-008).
$soak2 = Start-Process -FilePath $app `
    -ArgumentList "--encode-test","--seconds","7200","--preset","standard","--output",$clipDir `
    -PassThru -WindowStyle Hidden -Wait
Log "soak-2h-standard exit=$($soak2.ExitCode)"
$clip2 = Get-ChildItem -Path $clipDir -Filter "*.mp4" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$v2 = Start-Process -FilePath $validator `
    -ArgumentList $clip2.FullName,"--expect-width","1920","--expect-height","1080","--expect-fps","30","--expect-duration-sec","7200","--tolerance-sec","5","--expect-frames","216000" `
    -PassThru -WindowStyle Hidden -Wait
Log "soak-2h-standard validator exit=$($v2.ExitCode)"
Log "watchdog finished"
exit 0
