# Virtual Screen Recorder - test script (T003, NFR-036).
#
# Builds and runs the unit tests; media validator is built for integration
# use. Results land in artifacts/test-results/.

param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "common.ps1")

Write-Host "== Virtual Screen Recorder tests ($Configuration|x64) =="
$msbuild = Get-MsBuildPath

& $msbuild (Join-Path $root "tests\UnitTests\UnitTests.vcxproj") "/restore" `
    "/p:Configuration=$Configuration" "/p:Platform=x64" "/v:m" "/nologo"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Test build FAILED." -ForegroundColor Red
    exit $LASTEXITCODE
}

$resultsDir = Join-Path $root "artifacts\test-results"
New-Item -ItemType Directory -Force -Path $resultsDir | Out-Null

$testsExe = Join-Path $root "artifacts\$Configuration\tests\UnitTests.exe"
if (-not (Test-Path $testsExe)) {
    Write-Host "UnitTests.exe not found at $testsExe" -ForegroundColor Red
    exit 1
}

$logPath = Join-Path $resultsDir "unit-tests-$Configuration.log"
& $testsExe 2>&1 | Tee-Object -FilePath $logPath
$exit = $LASTEXITCODE

if ($exit -eq 0) {
    Write-Host "Unit tests PASSED (log: $logPath)" -ForegroundColor Green
} else {
    Write-Host "Unit tests FAILED (log: $logPath)" -ForegroundColor Red
}
exit $exit
