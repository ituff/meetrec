# Virtual Screen Recorder - driver removal helper (T012).
#
# Removes every staged VirtualDisplayDriver.inf package from the driver store
# and uninstalls any present device instances. Requires elevation.

$ErrorActionPreference = "Stop"

$pnputil = Join-Path $env:windir "System32\pnputil.exe"

# Find staged third-party INFs whose original name matches ours.
$enumOutput = & $pnputil /enum-drivers
$found = $false
$current = @{}
$entries = @()

foreach ($line in $enumOutput) {
    if ($line -match "^(Published Name|发布名称):\s*(.+)$") { $current["pub"] = $Matches[2].Trim() }
    elseif ($line -match "^(Original Name|原始名称):\s*(.+)$") { $current["orig"] = $Matches[2].Trim() }
    elseif ($line -match "^$" -or $null -eq $line) {
        if ($current.Count -gt 0) { $entries += [pscustomobject]$current; $current = @{} }
    }
}
if ($current.Count -gt 0) { $entries += [pscustomobject]$current }

foreach ($e in $entries) {
    if ($e.orig -and $e.orig -eq "VirtualDisplayDriver.inf") {
        $found = $true
        Write-Host "Deleting driver package $($e.pub)"
        & $pnputil /delete-driver $e.pub /force /uninstall
    }
}

if (-not $found) {
    Write-Host "No staged VirtualDisplayDriver.inf packages found."
}
exit 0
