# Task completion notes (specs/001-mvp/tasks.md §1.7)

Implementation record for the MVP P0 task list, executed 2026-09-09. Requirement IDs, files,
commands, results, and remaining machine-dependent evidence are listed per milestone. The
repository was implemented as one baseline change set; follow-up evidence runs (T091-T093) are
called out explicitly rather than silently omitted.

## M0 — T001, T002, T003 (NFR-031..NFR-036)

- **Delivered:** repository skeleton per design.md §3; `.gitignore` (build outputs, catalogs,
  certs/keys, recordings, logs); README (scope, prerequisites, build/test commands, test-signing
  warning, platform); ADR template; `VirtualScreenRecorder.sln` (x64-only + ReleaseSigned mapped
  to Release project configs); `Directory.Build.props` (C++20, Unicode, `/W4 /WX`, `/utf-8`,
  artifact output conventions; driver project excluded by name); `scripts/build.ps1`,
  `test.ps1`, `clean.ps1`, `common.ps1` with VS/SDK/WDK detection, actionable failure messages
  (`VSR-BLD-00x`), and `artifacts/build-manifest.json`.
- **Verified on this machine:** `build.ps1` (WDK missing) exits **2** with an actionable WDK
  message (the T003 acceptance row); `build.ps1 -SkipDriver` builds app + tests and writes the
  manifest; `clean.ps1` restores the tree; `test.ps1` runs the unit suite.

## M1 — T010, T011, T012 (FR-010..FR-017, NFR-021, NFR-035)

- **Delivered:** `src/Driver` (UMDF 2/IddCx driver adapted from the Microsoft IndirectDisplay
  sample with per-file attribution): single adapter/monitor, product modes
  720p30/1080p30(preferred)/1080p60, product EDID (`scripts/gen-edid.py`, checksum-verified),
  stable container ID, WPP lifecycle tracing, x64-only vcxproj, INF with
  `VirtualScreenRecorder` identities and unique UMDF host group. `docs/sample-audit-idd.md`
  records every imported file, every sample TODO disposition, and removals (T010 done-criteria).
  ADR-002 finalized the SwDeviceCreate lifecycle; ADR drafts ADR-001..ADR-006 created.
  `scripts/install-driver.ps1`, `remove-driver.ps1`, `testsign.ps1` (test-signing workflow,
  keys never committed).
- **App side:** `DisplayControl` module — `TopologyService` (QueryDisplayConfig + HMONITOR
  mapping + identity match, unit-tested), `TopologyWatcher` (debounced hidden-window watcher),
  `SoftwareDisplayDevice` (SwDeviceCreate lifecycle, SetupAPI enable/disable, driver-presence
  check).
- **Remaining evidence (requires WDK + test machine):** T011 install/modes, T012 ten-cycle +
  reboot identity stability — checklists prepared in `docs/driver-manual-test-log.md`.

## M2 — T020..T031 (FR-040..FR-063, NFR-005..NFR-007, NFR-010..NFR-012)

- **Delivered:** `Capture` module — `GraphicsDevice` (BGRA D3D11 bootstrap with typed
  `VSR-INIT-D3D-FAILED` mapping and device-removed probe), WGC interop (local
  `IGraphicsCaptureItemInterop` declaration, CreateForMonitor), `BoundedFrameQueue`
  (capacity 3, drop-oldest, atomic counters, close/drain semantics), `MonitorCaptureSession`
  (free-threaded frame pool, pooled-copy ownership per ADR-005, content-size adaptation,
  preview tap). `Encoder` module — `VideoPreset` (spec table + validation), `EncoderProbe`
  (MFTEnumEx + deterministic selection policy; fails `VSR-ENCODER-UNAVAILABLE` before any
  output exists), `VideoConverter` (D3D11 VideoProcessor BGRA→NV12, GPU-only normal path),
  `H264Mp4Writer` (sink writer, HW D3D11 path + documented software fallback, monotonic clamp,
  `.partial.mp4`, idempotent finalize).
- **Verified:** unit tests for queue semantics/conservation, presets, encoder policy.
- **Remaining evidence:** T022 60-second spike, T023 30-minute soak, T030 vendor probe outputs,
  T031 per-preset encodes — run via `--dev-record` + `MediaValidator` on a machine with the
  driver (see `tests/IntegrationTests/IntegrationTests.ps1`).

## M3 — T040, T041, T042 (FR-055..FR-077, FR-052..FR-060, NFR-012..NFR-014)

- **Delivered:** `Application` module — `Coordinator` (sole lifecycle owner; serialized
  commands; worker-thread pipeline; per-session correlation ID; named single-recording mutex;
  display/target/device/disk monitors; rollback ordering on Preparing failures; idempotent
  Stop; bounded-wait shutdown), `RecordingTimeline` (pause-excluded monotonic timestamps,
  unit-tested), `AppState` (spec §8 transition/command tables, fully unit-tested),
  `OutputFile` (naming/collision/validation), `DiskSpaceGuard` (documented threshold
  max(256 MB, 30 s @ bitrate), injectable probe), `Diagnostics` (sanitized metadata + log
  archive, PowerShell fallback to log folder).
- **Verified:** `--dev-record` headless entry point executes end-to-end and on this machine
  (no driver) terminates with the exact actionable error `VSR-INIT-D3D`→`VSR-INIT-DRIVER-MISSING`
  path and exit code 2; 56 unit tests pass (state machine complete transition-branch coverage,
  timeline math, queue conservation, file naming, disk thresholds).
- **Remaining evidence:** 5-minute end-to-end and pause/resume media checks (T040/T041) on a
  driver-enabled machine.

## M4 — T050..T061, T070..T082 (FR-020..FR-030, FR-080..FR-096, NFR-040..NFR-044)

- **Delivered:** `WindowManagement` — eligibility-filtered enumeration (visible/unowned/
  non-cloaked/non-tool/non-shell, recorder excluded, stale-HWND PID revalidation),
  placement/restore per design §4.5 with primary-monitor fallback and hung-window guard,
  unit-tested destination geometry. `App` — WinMain (COM/WinRT MTA, Win11 gate, single-instance
  mutex + registered activation message, `--dev-record`), `MainWindow` (target list, output
  folder via IFileDialog, presets, cursor toggle, state/duration/FPS/dropped/error panel,
  snapshot-driven command enabling, keyboard navigation via IsDialogMessage, Per-Monitor V2 +
  WM_DPICHANGED re-layout, settings persistence), `TrayIcon` (state-colored runtime icons,
  state+elapsed tooltip, Explorer-restart recreation, close-hides-to-tray, exit confirmation),
  `PreviewPanel` (≈10 FPS observability-only GDI present from CPU tap; closing it never affects
  capture).
- **Verified:** full Debug|x64 build with `/W4 /WX` (0 errors/warnings), unit suite green.
- **Remaining evidence:** DPI 100/150/200 %, mixed-DPI placement, tray/Explorer-restart and
  multi-app placement runs — `docs/manual-test-checklist.md`; T081 failure-injection rows are
  implemented (target loss, display loss, device loss, disk guard, write failure, repeated
  shutdown) with bounded waits and are exercised manually per checklist section 4/5.

## M5 — T090..T093 (FR-001..FR-007, NFR-020..NFR-023, AC-001/AC-010)

- **Delivered:** `installer/VirtualScreenRecorderSetup.iss` (preflight Win11 x64 + running-app
  guard, elevated driver staging via pnputil with surfaced exit codes, shortcuts, silent
  switches, uninstall removes driver and preserves user data) and `installer/README.md`.
  Test-signing hooks live in `scripts/testsign.ps1` with no secrets in the repository
  (NFR-022); production signing is documented as the T093 gate (ADR-006).
- **Remaining evidence:** T090 clean install/uninstall on a disposable machine, T091
  performance/soak measurements, T092 two-machine/two-GPU matrix, T093 signed distribution gate
  — harnesses and checklists delivered; execution requires physical machines.

## Commands and results (this machine)

```text
scripts\build.ps1                                   → exit 2 (missing WDK, actionable VSR-BLD-004)  ✓ per T003
scripts\build.ps1 -SkipDriver                       → exit 0, build-manifest.json written           ✓
scripts\test.ps1 (UnitTests)                        → 56 tests, 0 failing checks                    ✓
VirtualScreenRecorder.exe --dev-record --seconds 2  → exit 2, VSR-INIT-DRIVER-MISSING actionable    ✓
MediaValidator.exe (usage)                          → runs; used by integration harness             ✓
```

## Limitations and follow-ups

1. Driver build/run evidence (T011/T012/T022/T023/T030/T031/T040-T042 media checks) requires a
   WDK + test-signed machine; all harnesses and logs are prepared.
2. WGC yellow border suppression (`IsBorderRequired` consent) is out of MVP scope — documented
   in `docs/known-limitations.md`.
3. `ReleaseSigned` solution configuration maps to Release project configs; production signing
   runs through the T093 pipeline once a certificate/attestation flow is available.
4. Preview tap uses a bounded CPU copy at ≤ 10 FPS (observability only); the recording path
   remains GPU-only — documented in code and design alignment notes.

---

# Continuation session (2026-09-10, before 09:00)

## Defect fixes found by review

1. **Target list was never drawn** — the owner-draw listbox (LBS_OWNERDRAWFIXED) had no
   WM_MEASUREITEM/WM_DRAWITEM handling, so the eligible-window list rendered blank (FR-021).
   Implemented icon + "exe - title" rows with DPI-scaled metrics, selection colors, and focus
   rect.
2. **TopologyWatcher never terminated** — message-only windows do not process WM_CLOSE via
   DefWindowProc, so `Stop()` always waited out its 5 s timeout and leaked the pump thread.
   Added WM_CLOSE → DestroyWindow and WM_DESTROY → PostQuitMessage.
3. **Preview toggle had no live effect** — the checkbox now enables/disables the preview panel
   immediately and clears cached frames (FR-048 observability-only rule unaffected).
4. **Settings were only saved from the diagnostics dialog** — now saved on window destroy and
   after folder selection (FR-052).
5. **Idle CPU risk** — `GetStatus` queried the full display topology on every 100 ms UI timer
   tick; the liveness check is now cached with a 2 s cadence (NFR-001).

## P1 backlog items implemented

- **T100 (FR-029):** `window::PopupTracker` — WinEvent hook (EVENT_OBJECT_SHOW, target PID) on
  a dedicated pump thread; new target-process windows are fitted into the virtual work area
  with SWP_NOZORDER|SWP_NOACTIVATE (no focus steal). Decision logic is pure and covered by
  8 unit tests (process boundary, tool/cloaked/borderless windows, ownership link, main-frame
  exclusion). Coordinator starts/stops it around the recording session; failures are non-fatal.
- **T103:** `CrashReporter` — SetUnhandledExceptionFilter + MiniDumpWriteDump into
  `%LOCALAPPDATA%\VirtualScreenRecorder\CrashDumps`; strictly opt-in via the new
  `enableCrashDumps` settings field (default false, never uploaded). Settings schema extended
  with unknown-field preservation intact; wiring happens in `Application::InitRuntime`.
- **T102:** `docs/spike-system-audio.md` — WASAPI loopback / AAC / sink-writer feasibility
  report with privacy UX requirements; explicitly no audio code (per task stop rule).
- **T104:** `docs/enterprise-deployment-assessment.md` — Intune wrap vs WiX/MSI vs MSIX and the
  driver pre-staging recommendation.

## Verification (this session)

```text
Debug|x64 full rebuild:          0 errors, 0 warnings (/W4 /WX)   ✓
Release|x64 full rebuild:        0 errors, 0 warnings (/W4 /WX)   ✓
Unit tests (Debug and Release):  64 tests, 0 failing checks       ✓
```

## Follow-ups unchanged from baseline

Driver/build-machine evidence (T011/T012/T022/T023/T030/T031/T040-T042 media checks, T090-T093
execution) remains pending physical machines, as recorded above.

---

# Continuation session 2 (2026-09-10, before 09:00)

## New: `--self-check` diagnostics mode

`VirtualScreenRecorder.exe --self-check` prints a read-only capability report: OS build, settings
health, the created D3D11 adapter + feature level, Windows.Graphics.Capture availability, the
H.264 encoder probe result (all candidate MFTs plus the selected hardware/software transform),
virtual display driver/monitor state, and a sanitized topology summary. Exit 0 when core
capabilities exist; never mutates state.

**Evidence captured on the development machine (AMD graphics):**

```text
[ OK ] Direct3D 11: adapter=AMD Radeon(TM) Graphics, featureLevel=0xB000
[ OK ] Graphics capture: available
[ OK ] H.264 encoders: selected=AMDh264Encoder (hardware, D3D11-aware), candidates=12
[ OK ] Display topology: 1 active monitor(s)
```

This records the T020 "diagnostic reports adapter and feature level" item and the T030
encoder-probe output on an AMD machine; Intel/NVIDIA probes remain part of T092.

## FR-016 completed in the UI

The controller now has a Connect/Disconnect display button (idle states only; coordinator still
rejects disconnect while active — FR-017) plus a "Virtual display: active/inactive (WxH)" status
line. Connect runs on a worker with a timer-reap pattern (bounded waits; UI stays responsive).

## Hardening and polish

- Tray notifications throttled: `Shell_NotifyIcon` only on state change or displayed-second
  change, not on every 100 ms timer tick (NFR-001).
- NFR-023: `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
  LOAD_LIBRARY_SEARCH_SYSTEM32)` at process start; the product ships no side DLLs.
- Diagnostics package now embeds real runtime facts: OS build string, GPU adapter description,
  probed encoder name + hardware flag, and virtual display driver presence (FR-096).
- First-run settings: self-check treats a missing `settings.json` as OK (normal first run) and
  only flags an existing-but-invalid file.

## Verification (this session)

```text
Debug|x64 rebuild:    0 errors, 0 warnings (/W4 /WX)   ✓
Release|x64 rebuild:  0 errors, 0 warnings (/W4 /WX)   ✓
Unit tests:           64 tests, 0 failing checks (both configs) ✓
--self-check:         runs to completion, exit 0, exit 1 path not exercised here
```
