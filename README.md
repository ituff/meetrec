# Virtual Screen Recorder

A lightweight Windows 11 application that creates **one virtual extended display**, moves a
selected application window onto it, and records the virtual display to an **H.264 MP4** file —
while you keep working on your physical monitor. Notifications, unrelated windows, and mouse
activity on the physical desktop never appear in the recording.

- **Spec:** [`specs/001-mvp/spec.md`](specs/001-mvp/spec.md) (VSR-MVP-001 v1.0)
- **Requirements:** [`specs/001-mvp/requirements.md`](specs/001-mvp/requirements.md)
- **Design:** [`specs/001-mvp/design.md`](specs/001-mvp/design.md)
- **Tasks:** [`specs/001-mvp/tasks.md`](specs/001-mvp/tasks.md)

## Supported platform

| | |
|---|---|
| OS | Windows 11 x64 (build 22000+) |
| Session | Interactive local desktop session |
| GPU | Direct3D 11 capable GPU with current vendor driver |
| Filesystem | Local NTFS output target for acceptance testing |

Not supported in the MVP: Remote Desktop, Windows Sandbox, GPU-less servers, Windows 10,
multi-monitor virtual displays, audio capture, HDR. See
[docs/known-limitations.md](docs/known-limitations.md).

## Prerequisites

Application build (always required):

1. **Visual Studio 2022** (or Build Tools) with the **Desktop development with C++** workload
   - Tested toolchain: MSVC 14.44 (v143 toolset), Windows SDK **10.0.26100.0**
   - C++/WinRT headers ship with the SDK — no extra NuGet package is required.

Driver build (optional; only needed to run the virtual display):

2. **Windows Driver Kit (WDK)** matching the SDK above.
   Download: <https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk>

Internal test installs additionally require **Test Mode** for the test-signed driver
(see "Test signing warning" below).

## Building

```powershell
# Application + tests (works without the WDK)
powershell -File scripts\build.ps1 -Configuration Debug -SkipDriver

# Full build including the UMDF virtual display driver (requires WDK)
powershell -File scripts\build.ps1 -Configuration Debug

# Run the unit tests
powershell -File scripts\test.ps1

# Clean
powershell -File scripts\clean.ps1
```

Outputs follow the artifact convention (design.md §7.3):

```text
artifacts/<Configuration>/
  app/VirtualScreenRecorder.exe       # controller application
  driver/VirtualDisplayDriver.*       # UMDF driver package (WDK builds only)
  tests/UnitTests.exe                 # unit test runner
  tests/MediaValidator.exe            # test-only MP4 validation tool
  obj/…                               # static libraries and intermediates
  build-manifest.json                 # tool versions + commit id
```

## Running (development)

```text
VirtualScreenRecorder.exe                       # controller UI (also creates the tray icon)
VirtualScreenRecorder.exe --dev-record --seconds 10 --preset standard --output D:\Clips
```

`--dev-record` is the headless development entry point (task T040): it creates/locates the
virtual display, records it for the fixed duration at the chosen preset
(`low` / `standard` / `high`), finalizes the MP4, prints the pipeline counters, and exits with:

- `0` — success
- `2` — driver/display not available (`VSR-INIT-DRIVER-MISSING`, `VSR-DISPLAY-NOT-FOUND`)
- `3` — start rejected (state machine / validation)
- `4` — never reached the Recording state within the timeout
- `5` — recording stopped with an error (details printed)

Data locations (per user): `%LOCALAPPDATA%\VirtualScreenRecorder\`
(`settings.json`, `Logs\`, `Diagnostics\`).

## The virtual display driver

`src/Driver` is a UMDF 2 / IddCx indirect display driver derived from the
[Microsoft IndirectDisplay sample](https://github.com/microsoft/Windows-driver-samples/tree/main/video/IndirectDisplay)
(MIT license) with full attribution preserved in every file. The import audit lives in
[docs/sample-audit-idd.md](docs/sample-audit-idd.md). The driver exposes exactly **one** monitor
named **"Virtual Recording Display"** with modes 1280x720@30, **1920x1080@30 (preferred)** and
1920x1080@60, and a stable EDID/container identity so the application can find it regardless of
display ordinal.

The application creates the device on demand via `SwDeviceCreate` and removes it when the
recording session ends (ADR-002). The driver package must be staged in the driver store first:

```powershell
# One-time (test machines): enable test signing, then reboot
bcdedit /set testsigning on

# Build the driver (WDK machine), sign it, stage it (elevated PowerShell)
powershell -File scripts\build.ps1
powershell -File scripts\testsign.ps1
powershell -File scripts\install-driver.ps1

# Later: remove from the driver store
powershell -File scripts\remove-driver.ps1
```

### Test signing warning

The driver shipped in internal builds is **test-signed**. Installing it requires enabling
Windows Test Mode (`bcdedit /set testsigning on`) and trusting the generated certificate
(`vsr-test-signing.cer`) on the test machine. Test Mode degrades some OS security attestations —
use dedicated development machines only. **Never** commit certificates, private keys, or
generated `.cat` files (enforced by `.gitignore`); production signing is a separate release gate
(spec.md §13, ADR-006).

## Architecture overview

```text
Win32 Controller + Tray
  └─ Application Coordinator / State Machine
       ├─ DisplayControl ── UMDF 2 / IddCx driver ── Windows Display Stack
       ├─ WindowPlacementManager ── Win32 Window Manager
       ├─ CaptureSession ── Windows.Graphics.Capture ── D3D11 textures
       │                                                  │
       ├─ EncoderPipeline ←──── BoundedFrameQueue ────────┘
       │     └─ D3D11 VideoProcessor (BGRA→NV12) → MF H.264 MFT → MP4
       └─ Settings / Structured Log / Diagnostics
```

All first-party modules are static libraries linked into `VirtualScreenRecorder.exe`
(NFR-033); the driver remains a separate binary (NFR-034).

## Testing

- **Unit tests** (`tests/UnitTests`, 56 tests): state machine transition coverage, pause-aware
  timestamp math, bounded-queue policy including concurrent producer/consumer, JSON/settings
  round trips, output naming and collision handling, disk-guard thresholds, encoder selection
  policy, destination-rect geometry, error-code strings, identity matching, log redaction.
  Run with `scripts\test.ps1`.
- **Media validation** (`tests/IntegrationTests/MediaValidator`): test-only Source Reader based
  MP4 validator (container, H.264 stream, dimensions, fps, monotonic timestamps, full decode,
  duration tolerance).
- **Integration** (`tests/IntegrationTests/IntegrationTests.ps1`): end-to-end dev-record +
  media validation on a machine with the driver installed.
- **Manual**: [docs/manual-test-checklist.md](docs/manual-test-checklist.md) with evidence
  expectations; driver-specific evidence is captured per
  [docs/driver-manual-test-log.md](docs/driver-manual-test-log.md).
- **Traceability**: [docs/requirements-traceability-matrix.md](docs/requirements-traceability-matrix.md)
  maps every MUST requirement to tests or inspections.

## Documentation map

```text
docs/
  adr/                     # architecture decision records (ADR-001..ADR-006)
  sample-audit-idd.md      # every imported driver file, TODO review, removals
  known-limitations.md     # user-facing limitations and unsupported scenarios
  manual-test-checklist.md # release qualification checklist
  driver-manual-test-log.md# driver install/mode/reboot evidence log
  requirements-traceability-matrix.md
installer/
  VirtualScreenRecorderSetup.iss  # Inno Setup project (internal MVP)
scripts/
  build.ps1 / test.ps1 / clean.ps1 / common.ps1
  testsign.ps1 / install-driver.ps1 / remove-driver.ps1
  gen-edid.py              # regenerates src/Driver/Edid.h (checksum verified)
```

## License / attribution

Driver code in `src/Driver` is derived from Microsoft's
[Windows-driver-samples IndirectDisplay](https://github.com/microsoft/Windows-driver-samples)
sample (MIT License, Copyright (c) Microsoft Corporation). All other first-party code is part of
the Virtual Screen Recorder project. See [docs/sample-audit-idd.md](docs/sample-audit-idd.md).
