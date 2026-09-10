# Installs the WindowsUserModeDriver10.0 shadow platform toolset for the
# installed WDK (10.0.26100 or later) into the user-writable MSBuild tree.
#
# WDK 10.0.26100 installed via winget does not register the legacy
# "WindowsUserModeDriver10.0" platform toolset with Visual Studio BuildTools
# (that was the VSIX's job). This script creates a user-level toolset that
# imports the WDK build chain directly - no admin required.
#
# Idempotent: the toolset directory is rebuilt from pristine sources on every
# run. Invoked automatically by build.ps1.

param(
    [string]$WDKBuildFolder = '10.0.26100.0',
    [string]$VCTargetsSource = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Microsoft\VC\v170'
)

$ErrorActionPreference = 'Stop'

$wdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$dest = Join-Path $env:LOCALAPPDATA 'VsrVCTargets170'

# 1. Copy the VC targets tree (standard Microsoft.Cpp.* targets/props) so the
#    toolset's VCTargetsPath override resolves everything it needs.
if (-not (Test-Path (Join-Path $VCTargetsSource 'Microsoft.Cpp.props'))) {
    Write-Error "[VSR-TS-001] VC targets source not found: $VCTargetsSource"
    exit 1
}
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item -Path (Join-Path $VCTargetsSource '*') -Destination $dest -Recurse -Force

# 2. Rebuild the WindowsUserModeDriver10.0 toolset from scratch.
$ts = Join-Path $dest 'Platforms\x64\PlatformToolsets\WindowsUserModeDriver10.0'
if (Test-Path $ts) { Remove-Item $ts -Recurse -Force }
New-Item -ItemType Directory -Force -Path $ts | Out-Null
Copy-Item -Path (Join-Path $VCTargetsSource 'Platforms\x64\Platform.props') -Destination (Join-Path $ts 'Platform.props') -Force
Copy-Item -Path (Join-Path $VCTargetsSource 'Platforms\x64\Platform.targets') -Destination (Join-Path $ts 'Platform.targets') -Force

# 3. Toolset.props: v143's MSVC toolset imports + the WDK property block.
$v143Props = Join-Path $VCTargetsSource 'Platforms\x64\PlatformToolsets\v143\Toolset.props'
$txt = [System.IO.File]::ReadAllText($v143Props)
$wdkProps = @'
  <PropertyGroup>
    <IsUserModeToolset>true</IsUserModeToolset>
    <UMDF_VERSION_MAJOR>2</UMDF_VERSION_MAJOR>
    <UMDF_VERSION_MINOR>25</UMDF_VERSION_MINOR>
    <DriverTargetPlatform Condition="'$(DriverTargetPlatform)' == ''">Universal</DriverTargetPlatform>
    <VsrWdkDefaultProps>$(WDKContentRoot)build/$(WDKBuildFolder)/WindowsDriver.Default.props</VsrWdkDefaultProps>
    <VsrWdkSharedProps>$(WDKContentRoot)build/$(WDKBuildFolder)/WindowsDriver.Shared.Props</VsrWdkSharedProps>
    <VsrWdkPlatformProps>$(WDKContentRoot)build/$(WDKBuildFolder)/x64/ImportAfter/WDK.x64.WindowsUserModeDriver.Platform.props</VsrWdkPlatformProps>
    <WDK_UM_INC_PATH>$(WDKContentRoot)Include/$(WDKBuildFolder)/um/</WDK_UM_INC_PATH>
    <WDK_UM_LIB_PATH>$(WDKContentRoot)Lib/$(WDKBuildFolder)/um/x64/</WDK_UM_LIB_PATH>
    <UMDF_INC_PATH>$(WDKContentRoot)Include/wdf/umdf/</UMDF_INC_PATH>
    <UMDF_LIB_PATH>$(WDKContentRoot)Lib/wdf/umdf/x64/</UMDF_LIB_PATH>
    <KIT_SHARED_IncludePath>$(WDKContentRoot)Include/$(WDKBuildFolder)/shared</KIT_SHARED_IncludePath>
    <KIT_SHARED_INC_PATH_WDK>$(WDKContentRoot)Include/$(WDKBuildFolder)/shared</KIT_SHARED_INC_PATH_WDK>
    <DDK_LibraryPath_DDKPlatform>$(WDKContentRoot)Lib/$(WDKBuildFolder)/um/x64/</DDK_LibraryPath_DDKPlatform>
    <DDK_INC_PATH>$(WDK_UM_INC_PATH)</DDK_INC_PATH>
    <DDK_INC_PATH>$(WDK_UM_INC_PATH)</DDK_INC_PATH>
    <Inf2CatWindowsVersionList Condition="'$(Inf2CatWindowsVersionList)' == ''">10_X64</Inf2CatWindowsVersionList>
  </PropertyGroup>
  <Import Condition="Exists($(VsrWdkDefaultProps))" Project="$(VsrWdkDefaultProps)"/>
  <Import Condition="Exists($(VsrWdkSharedProps))" Project="$(VsrWdkSharedProps)"/>
  <Import Condition="Exists($(VsrWdkPlatformProps))" Project="$(VsrWdkPlatformProps)"/>
'@
# Insert right after the opening <Project ...> element so the WDK paths are
# defined before the WDK chain and v143 imports evaluate.
$idx = $txt.IndexOf('<Project')
$idx = $txt.IndexOf('>', $idx) + 1
$txt = $txt.Substring(0, $idx) + "`r`n" + $wdkProps + $txt.Substring($idx)
[System.IO.File]::WriteAllText((Join-Path $ts 'Toolset.props'), $txt, [System.Text.Encoding]::UTF8)

# 4. Toolset.targets: v143's + the WDK CX targets import (UMDF class extensions).
$v143Targets = Join-Path $VCTargetsSource 'Platforms\x64\PlatformToolsets\v143\Toolset.targets'
$ttxt = [System.IO.File]::ReadAllText($v143Targets)
$cxImport = @'
  <Import Project="$(WDKContentRoot)build\$(WDKBuildFolder)\WindowsDriver.UserMode.CX.targets" Condition="'$(IsUserModeToolset)' == 'true' and Exists('$(WDKContentRoot)build\$(WDKBuildFolder)\WindowsDriver.UserMode.CX.targets')"/>
'@
$idx2 = $ttxt.LastIndexOf('</Project>')
$ttxt = $ttxt.Substring(0, $idx2) + $cxImport + "`r`n" + $ttxt.Substring($idx2)
[System.IO.File]::WriteAllText((Join-Path $ts 'Toolset.targets'), $ttxt, [System.Text.Encoding]::UTF8)

Write-Host "Shadow toolset installed: $ts"
Write-Host "WDK build folder: $(Join-Path $wdkRoot "build\$WDKBuildFolder")"
exit 0
