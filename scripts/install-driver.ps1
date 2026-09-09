# Virtual Screen Recorder - driver install helper for test machines (T012).
#
# Stages the test-signed driver package into the Windows driver store so the
# application's SwDeviceCreate path can start the virtual display. Requires
# elevation and (for test-signed builds) Test Mode:
#     bcdedit /set testsigning on   (reboot required)
#
# Usage: powershell -File scripts\install-driver.ps1 [-Configuration Debug]

param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$driverDir = Join-Path $root "artifacts\$Configuration\driver"
$inf = Join-Path $driverDir "VirtualDisplayDriver.inf"

if (-not (Test-Path $inf)) {
    Write-Error @"
[VSR-DRV-001] Driver package not found at $driverDir.
Build the driver first (scripts\build.ps1; requires the WDK) and sign the
package (scripts\testsign.ps1).
"@
    exit 1
}

Write-Host "Staging driver package: $inf"
$pnputil = Join-Path $env:windir "System32\pnputil.exe"
& $pnputil /add-driver $inf /install
if ($LASTEXITCODE -ne 0) {
    Write-Host "pnputil failed with exit code $LASTEXITCODE. Is the package test-signed and is Test Mode enabled (bcdedit /set testsigning on)?" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "Driver staged. Launch VirtualScreenRecorder.exe and start a recording; the app creates the virtual display device on demand."
exit 0
