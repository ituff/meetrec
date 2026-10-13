# Virtual Screen Recorder - test signing helper (internal MVP only; T012).
#
# Creates a local self-signed code-signing certificate, test-signs the driver
# package (INF + catalog), and prints the commands needed to trust it on a
# development machine.
#
# *** TEST-SIGNED BUILDS REQUIRE TEST MODE ***
#     bcdedit /set testsigning on    (then reboot)
# Never ship the generated certificate or keys (NFR-022).
#
# Usage: powershell -File scripts\testsign.ps1 [-Configuration Debug]

param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$CertificateName = "VSR Test Signing (dev only)"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$driverDir = Join-Path $root "artifacts\$Configuration\driver"

if (-not (Test-Path (Join-Path $driverDir "VirtualDisplayDriver.inf"))) {
    Write-Error "[VSR-SGN-001] Driver package not found under $driverDir. Build first."
    exit 1
}

# Locate signtool from the Windows SDK.
$signtool = $null
$kitsRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
if (Test-Path $kitsRoot) {
    $signtool = Get-ChildItem -Path $kitsRoot -Recurse -Filter "signtool.exe" |
        Where-Object { $_.FullName -match "x64" } |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $signtool) {
    Write-Error "[VSR-SGN-002] signtool.exe not found under $kitsRoot. Install the Windows SDK."
    exit 1
}

# Create (or reuse) the self-signed certificate in the user store.
$cert = Get-ChildItem Cert:\CurrentUser\My |
    Where-Object { $_.Subject -eq "CN=$CertificateName" -and $_.HasPrivateKey } |
    Sort-Object NotAfter -Descending | Select-Object -First 1
if (-not $cert) {
    Write-Host "Creating self-signed test certificate '$CertificateName'..."
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=$CertificateName" `
        -KeyUsage DigitalSignature -FriendlyName $CertificateName `
        -CertStoreLocation Cert:\CurrentUser\My -NotAfter (Get-Date).AddYears(3)
}

# Export the public cert for trusting on other dev machines (never the key).
$cerPath = Join-Path $driverDir "vsr-test-signing.cer"
Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null
Write-Host "Public certificate exported: $cerPath"

# Stamp the INF (fills DriverVer, $ARCH$, $UMDFVERSION$) before cataloging.
$stampinf = Get-ChildItem -Path $kitsRoot -Recurse -Filter "stampinf.exe" |
    Where-Object { $_.FullName -match "x64" } |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $stampinf) {
    Write-Error "[VSR-SGN-004] stampinf.exe not found under $kitsRoot."
    exit 1
}
& $stampinf -f (Join-Path $driverDir "VirtualDisplayDriver.inf") -d * -a amd64 `
    -c "VirtualDisplayDriver.cat" -v "0.36.30.230" -u "2.25.0"
if ($LASTEXITCODE -ne 0) {
    Write-Error "[VSR-SGN-005] stampinf failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

# Sign every payload file, then (re)generate the catalog via inf2cat and sign it.
$cat = Join-Path $driverDir "VirtualDisplayDriver.cat"
if (Test-Path $cat) { Remove-Item $cat -Force }

$inf2cat = Get-ChildItem -Path $kitsRoot -Recurse -Filter "inf2cat.exe" |
    Where-Object { $_.FullName -match "x64" } |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $inf2cat) {
    # Some SDK layouts ship Inf2Cat only in the x86 tool directory; it runs on x64.
    $inf2cat = Get-ChildItem -Path $kitsRoot -Recurse -Filter "Inf2Cat.exe" |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if ($inf2cat) {
    & $inf2cat /driver:$driverDir /os:10_X64 /uselocaltime
    if ($LASTEXITCODE -ne 0) {
        Write-Error "[VSR-SGN-003] inf2cat failed with exit code $LASTEXITCODE."
        exit $LASTEXITCODE
    }
} else {
    Write-Warning "inf2cat.exe not found; expecting the WDK build to have produced the catalog."
}

& $signtool sign /fd SHA256 /a /n $CertificateName $cat
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $signtool sign /fd SHA256 /a /n $CertificateName (Join-Path $driverDir "VirtualDisplayDriver.dll")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "Package test-signed with '$CertificateName'."
Write-Host "On the test machine (one time):"
Write-Host "  1. Import $cerPath into 'Trusted People' (and 'Trusted Publishers') of the Local Machine store."
Write-Host "  2. Enable Test Mode:  bcdedit /set testsigning on   (reboot)"
Write-Host "  3. Run scripts\install-driver.ps1 -Configuration $Configuration (elevated)."
exit 0
