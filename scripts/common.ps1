# Virtual Screen Recorder - shared build helpers (T003).

function Get-VsInstallation {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw @"
[VSR-BLD-001] Visual Studio 2022 was not found (vswhere.exe missing).
Install Visual Studio 2022 (or Build Tools) with the "Desktop development with C++" workload.
See README.md -> Prerequisites.
"@
    }
    $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $path) {
        throw @"
[VSR-BLD-002] Visual Studio 2022 with the C++ workload was not found.
Install Visual Studio 2022 (or Build Tools) with "Desktop development with C++".
See README.md -> Prerequisites.
"@
    }
    return $path
}

function Get-MsBuildPath {
    $vsPath = Get-VsInstallation
    $msbuild = Join-Path $vsPath "MSBuild\Current\Bin\amd64\MSBuild.exe"
    if (-not (Test-Path $msbuild)) {
        throw "[VSR-BLD-003] MSBuild.exe not found under $vsPath."
    }
    return $msbuild
}

function Test-WdkInstalled {
    # The WDK integrates the WindowsUserModeDriver10.0 platform toolset and
    # ships the driver build props under Windows Kits\<ver>\build.
    $roots = @(
        (Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\build"),
        (Join-Path $env:ProgramFiles "Windows Kits\10\build")
    )
    foreach ($root in $roots) {
        if (Test-Path $root) { return $true }
    }
    return $false
}

function Assert-WdkInstalled {
    if (-not (Test-WdkInstalled)) {
        throw @"
[VSR-BLD-004] The Windows Driver Kit (WDK) is not installed.
The virtual display driver project requires the WDK matching the installed
Windows SDK (10.0.26100.0). Download: https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk
Re-run with -SkipDriver to build only the application and tests.
"@
    }
}

function Get-GitCommitId {
    try {
        $id = & git rev-parse --short HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $id) { return $id }
    } catch { }
    return "nogit"
}

function Write-BuildManifest {
    param(
        [string]$SolutionRoot,
        [string]$Configuration,
        [hashtable]$Extra = @{}
    )
    $vsPath = Get-VsInstallation
    $manifest = [ordered]@{
        product        = "MeetRec"
        configuration  = $Configuration
        commit         = Get-GitCommitId
        builtAtUtc     = (Get-Date).ToUniversalTime().ToString("o")
        visualStudio   = $vsPath
        windowsSdk     = "10.0.26100.0"
        wdkPresent     = Test-WdkInstalled
        machine        = $env:COMPUTERNAME
    }
    foreach ($k in $Extra.Keys) { $manifest[$k] = $Extra[$k] }

    $artifacts = Join-Path $SolutionRoot "artifacts"
    New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
    $path = Join-Path $artifacts "build-manifest.json"
    ($manifest | ConvertTo-Json) | Set-Content -Path $path -Encoding UTF8
    Write-Host "Build manifest: $path"
}
