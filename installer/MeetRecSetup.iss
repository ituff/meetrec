; ============================================================================
;  Virtual Screen Recorder - internal MVP installer (T090; ADR-006)
;  Inno Setup 6.x. Build:
;    iscc /DConfiguration=Release installer\MeetRecSetup.iss
;
;  Silent switches (FR-007): /SILENT /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
;  The driver payload must be test-signed (scripts\testsign.ps1) and the
;  target machine must have Test Mode enabled + the certificate trusted.
;  This is clearly labeled in the wizard (internal build only).
; ============================================================================

#define AppName "MeetRec"
#define AppVersion "1.1.0.0"
#define AppPublisher "Allen Cheng"
#define AppExe "MeetRec.exe"
#ifndef Configuration
#define Configuration "Release"
#endif

[Setup]
AppId={{6B1E5D2A-93C7-4A6E-9C04-2F8D51A7B301}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\MeetRec
DefaultGroupName=MeetRec
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=MeetRecSetup
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
; Elevation is required for Program Files + driver staging (FR-002).
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayIcon={app}\{#AppExe}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
TestSigningWarning=INTERNAL TEST BUILD: The bundled virtual display driver is test-signed.%n%nThe target machine must have Windows Test Mode enabled (bcdedit /set testsigning on, then reboot) and must trust the signing certificate (vsr-test-signing.cer).%n%nDo not distribute this build externally.

[Files]
; Application (FR-003: runs unelevated; manifest requests asInvoker).
Source: "..\artifacts\{#Configuration}\app\MeetRec.exe"; DestDir: "{app}"; Flags: ignoreversion
; Driver payload (staged into {app}\driver, installed via pnputil below).
Source: "..\artifacts\{#Configuration}\driver\VirtualDisplayDriver.dll"; DestDir: "{app}\driver"; Flags: ignoreversion
Source: "..\artifacts\{#Configuration}\driver\VirtualDisplayDriver.inf"; DestDir: "{app}\driver"; Flags: ignoreversion
Source: "..\artifacts\{#Configuration}\driver\VirtualDisplayDriver.cat"; DestDir: "{app}\driver"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\artifacts\{#Configuration}\driver\vsr-test-signing.cer"; DestDir: "{app}\driver"; Flags: skipifsourcedoesntexist

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"

[Run]
; Stage the driver package into the driver store. Exit 0x103 = reboot
; required and is acceptable; other failures are surfaced below.
Filename: "{sys}\pnputil.exe"; Parameters: "/add-driver ""{app}\driver\VirtualDisplayDriver.inf"" /install"; \
  Flags: runhidden waituntilterminated; StatusMessage: "Staging virtual display driver..."; \
  AfterInstall: DriverStageResult(ExitCode)

Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Remove the driver package; /uninstall also disables present devices (AC-010).
Filename: "{sys}\pnputil.exe"; Parameters: "/delete-driver VirtualDisplayDriver.inf /uninstall"; Flags: runhidden; RunOnceId: "RemoveDriver"

[UninstallDelete]
; Recordings and settings under {localappdata} are intentionally preserved
; (AC-010): nothing under %LOCALAPPDATA%\VirtualScreenRecorder is deleted here.

[Code]
var
  DriverStaged: Boolean;
  DriverExitCode: Cardinal;

procedure DriverStageResult(ExitCode: Integer);
begin
  DriverExitCode := ExitCode;
  DriverStaged := (ExitCode = 0) or (ExitCode = 259); // 259 (0x103) = reboot required
  Log(Format('pnputil /add-driver finished with exit code %d (staged=%s)',
    [ExitCode, IntToStr(Ord(DriverStaged))]));
  if not DriverStaged then
    MsgBox(Format(
      'The virtual display driver could not be staged (pnputil exit code %d).'#13#10 +
      'Most likely the package signature is not trusted on this machine.'#13#10 +
      'Import vsr-test-signing.cer into the Local Machine "Trusted People" store,'#13#10 +
      'enable Test Mode (bcdedit /set testsigning on), reboot, then repair-install.',
      [ExitCode]), mbError, MB_OK);
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  DriverStaged := False;
  // Internal test-signed build disclosure (not shown in silent mode so
  // /VERYSILENT test automation stays unattended).
  if (not WizardSilent()) then
    MsgBox(ExpandConstant('{cm:TestSigningWarning}'), mbInformation, MB_OK);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  BuildStr: String;
  Build: Integer;
begin
  Result := '';
  // Preflight: Windows 10 2004+ / Windows 11 x64 (FR-001, NFR-030).
  if (not RegQueryStringValue(HKLM, 'SOFTWARE\Microsoft\Windows NT\CurrentVersion',
       'CurrentBuildNumber', BuildStr))
     or (not TryStrToInt(BuildStr, Build))
     or (Build < 19041) then
  begin
    Result := 'MeetRec requires Windows 10 version 2004 (x64) or later.';
    Exit;
  end;

  // Stop a running recorder before replacing files (upgrade handling).
  if FindWindowByClassName('VsrMainWindow') <> 0 then
  begin
    MsgBox('MeetRec is running. Close it (and stop any recording) before installing.',
      mbError, MB_OK);
    Result := 'MeetRec is running. Close it before installing.';
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if DriverStaged then
      Log('Driver package staged successfully.')
    else
      Log('App-only install completed (driver not staged).');
  end;
end;
