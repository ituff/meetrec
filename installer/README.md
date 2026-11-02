# Installer (internal MVP)

`MeetRecSetup.iss` is an [Inno Setup 6](https://jrsoftware.org/isinfo.php) project
producing **one Setup executable** that installs the desktop application and stages the
test-signed virtual display driver (FR-001, FR-002; ADR-006).

## Building the installer

1. Build and sign the payload first:
   ```powershell
   powershell -File scripts\build.ps1 -Configuration Release
   powershell -File scripts\testsign.ps1 -Configuration Release   # signs cat + dll, exports .cer
   ```
2. Compile:
   ```powershell
   iscc /DConfiguration=Release installer\MeetRecSetup.iss
   ```
   Output: `installer\Output\MeetRecSetup.exe`

## What it does

| Step | Detail |
|---|---|
| Preflight | Windows 10 2004+ / Windows 11 x64; recorder not running (upgrade safety) |
| Elevation | Requires admin (Program Files + `pnputil` driver staging, FR-002) |
| Files | `MeetRec.exe` → `{pf}\MeetRec`; driver payload → `{pf}\MeetRec\driver` |
| Driver | `pnputil /add-driver VirtualDisplayDriver.inf /install` — the driver store is staged; the *application* creates the actual device on demand (`SwDeviceCreate`), so no display appears until a recording starts |
| Shortcuts | Start Menu group |
| Post-install | Optional app launch (skipped in silent mode) |
| Uninstall | Stops/removes files, `pnputil /delete-driver … /uninstall`; **preserves** `%LOCALAPPDATA%\VirtualScreenRecorder` (settings, logs, recordings — AC-010) |

## Silent deployment (FR-007)

```text
MeetRecSetup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
MeetRecSetup.exe /UNINSTALL /SILENT      (via the uninstall entry point)
```

Exit codes follow Inno Setup conventions (`0` success; `3010`-style reboot hints come from the
driver step being logged and non-blocking).

## Test-signing prerequisites (internal builds only)

1. `bcdedit /set testsigning on` + reboot on the target machine.
2. Import `vsr-test-signing.cer` into Local Machine → Trusted People (and Trusted Publishers).
3. Then run the installer. When staging fails, the installer reports the `pnputil` exit code and
   remediation instead of failing silently (FR-006).

Production-signed distribution (no Test Mode) is release gate T093 / ADR-006 and is **not**
part of this internal installer.
