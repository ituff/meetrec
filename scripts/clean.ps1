# Virtual Screen Recorder - clean script (T003).
param()

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$artifacts = Join-Path $root "artifacts"
$vsDir = Join-Path $root ".vs"

foreach ($dir in @($artifacts, $vsDir)) {
    if (Test-Path $dir) {
        Remove-Item -Recurse -Force $dir
        Write-Host "Removed $dir"
    }
}

# Keep the artifacts folder (gitignored) with a .gitkeep placeholder.
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
New-Item -ItemType File -Force -Path (Join-Path $artifacts ".gitkeep") | Out-Null
Write-Host "Clean complete."
exit 0
