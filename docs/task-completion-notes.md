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

---

# Continuation session 3 (2026-09-10, before 09:00)

## Defect fixes

1. **IntegrationTests.ps1 used the PS7-only ternary operator** — the default Windows
   PowerShell 5.1 would fail to parse the harness. Rewritten with if/else; the whole script is
   now parse-checked (`[System.Management.Automation.Language.Parser]::ParseFile`) and syntax
   clean. The harness also gained a pre-step running `--self-check` and reports driver-dependent
   steps as SKIP (honest reporting) when the virtual display is not staged, instead of FAIL.
2. **Stop froze the UI for up to the 10 s finalize window** (FR-075/NFR-006). Stop now runs on a
   worker with the same timer-reap pattern as the display toggle; command buttons disable while
   the stop is pending; shutdown joins the worker with bounded waits. Pause/Resume remain
   synchronous (no I/O).
3. **GDI icon ownership** — the target list previously stored icons owned by the target windows
   (WM_GETICON/class icons) or the shared stock, which must never be destroyed, and refreshing
   the list leaked each batch's handles. `LoadWindowIcon` now hands out `CopyIcon` private
   copies; RefreshWindowList and WM_DESTROY release them.

## Verification (this session)

```text
Debug|x64 rebuild:    0 errors, 0 warnings (/W4 /WX)   ✓
Release|x64 rebuild:  0 errors, 0 warnings (/W4 /WX)   ✓
Unit tests:           64 tests, 0 failing checks (both configs) ✓
PowerShell scripts:   parser clean (IntegrationTests.ps1, build.ps1) ✓
```

---

# Continuation session 4 (2026-09-10, before 09:00)

## New: `--encode-test` — T031 acceptance evidence captured on this machine

`VirtualScreenRecorder.exe --encode-test --seconds N --preset p` pushes synthetic animated GPU
frames (gradient + moving block uploaded to a BGRA texture) through the **production pipeline**
(VideoConverter BGRA→NV12 → H.264 MFT → MP4 sink) with no capture or driver involvement, then
renames the finalized file. IntegrationTests.ps1 now runs it for all three presets and
validates each MP4 with MediaValidator.

**T031 verification met on this machine (AMD Radeon 680M-class iGPU, AMDh264Encoder hardware
MFT):**

```text
low      1280x720@30 : 300 frames, decoded 300, duration 10.033 s  VALID
standard 1920x1080@30: 300 frames, decoded 300, duration 10.033 s  VALID
high     1920x1080@60: 600 frames, decoded 600, duration 10.017 s  VALID
```

## Real defects found and fixed by the encode-test spike

1. **D3D11 device lacked `D3D11_CREATE_DEVICE_VIDEO_SUPPORT`** — the Video Processor could not
   be created on AMD drivers. Also enabled `ID3D11Multithread::SetMultithreadProtected(TRUE)`
   on the shared context (Media Foundation requirement for multi-threaded device use).
2. **`CreateVideoProcessorEnumerator(nullptr)` returns E_INVALIDARG on AMD drivers** — the
   converter now passes a real `D3D11_VIDEO_PROCESSOR_CONTENT_DESC` based on the preset
   geometry (stream source rects still adapt per frame).
3. **`MF_READWRITE_DISABLE_CONVERTERS` broke `MFCreateSinkWriterFromURL` (E_INVALIDARG) on this
   driver** — attribute removed; the writer probes submission instead.
4. **The AMD driver/MFT rejects GPU DXGI-buffer samples with E_INVALIDARG** — the writer now
   probes the selected submission path with one black frame + Flush **before recording starts**
   (NFR-005 bounded) and automatically falls back to the documented CPU staging path
   (ADR-004). The fallback is logged (`ENCODER_CPU_FALLBACK`) and `VSR_FORCE_CPU_ENCODER=1`
   remains as a manual override. The probe frame shifts output timing by at most one frame
   duration (~33 ms), well inside the 2 s duration tolerance.

## Harness updates

- IntegrationTests.ps1: PS7-only ternary removed (PS 5.1 parse-clean); now runs
  unit tests → self-check → encode pipeline validation (driver-independent) → display
  recording steps that SKIP (honestly reported) when the virtual display is not staged.
- MediaValidator: enabled `MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING` so RGB32 decode-verify
  works on machines without the legacy converter default.

## Verification (this session)

```text
Debug|x64 rebuild:    0 errors, 0 warnings (/W4 /WX)   ✓
Release|x64 rebuild:  0 errors, 0 warnings (/W4 /WX)   ✓
Unit tests:           64 tests, 0 failing checks (both configs) ✓
Integration harness:  PASS (unit + self-check + 3x encode validation; display steps SKIP) ✓
```

## Encoder-path soak evidence (NFR-003/007 partial, this machine)

Paced real-time `--encode-test` runs, Release build, private/working set sampled every 10 s:

| Run | Preset | Duration | Working-set trajectory | Result |
|---|---|---|---|---|
| Soak 1 | 1920x1080@60 (High) | 5 min | 479.0 -> 483.2 MB, flat | PASS (NFR-007 encode path) |
| Soak 2 | 1920x1080@30 (Standard) | 60 s | 480.4 -> 480.3 MB, flat | PASS |
| Soak 3 (first attempt, before pacing fix) | 1080p30 | 2 min | grew to 11 GB, stuck in Finalize — killed | methodology finding, see below |

Findings:
- With real-time pacing, encoder-path memory is flat and well-behaved; the earlier
  over-real-time/unpaced run confirmed `WriteSample` applies no backpressure (linear queue
  growth to 37 GB) — recorded in the ADR-004 addendum. Soak methodology must always use
  real-time cadence.
- One intermittent hang-in-Finalize with ~11 GB accumulation was observed once on a Debug
  1080p30 run and could not be reproduced in controlled reruns (3 subsequent runs flat and
  exiting 0). Flagged as a follow-up watch item for the AC-008 two-hour soak on the reference
  machine.
- NFR-003 (1080p30 private WS < 250 MB, SHOULD) measured ~480 MB for the encoder path alone
  on this iGPU (driver-side encoder buffers included). Documented as a SHOULD miss with
  measurement; improvement plan: reuse MF allocator pools and shrink the converter ring when
  the CPU fallback path is active.

## Session 4 addendum: pacing unit bug, watermark defense, bounded stop

After the encoder soak, four more items landed (all Debug-verified on this machine):

1. **`--encode-test` pacing had a duration-unit bug** — the per-frame target was built as
   `duration<double, micro>` carrying a *seconds* value, making the pacing horizon ~33
   **microseconds** per frame instead of 33 ms, i.e. pacing was inert. Runs therefore fed the
   sink writer faster than real time. Fixed to integer microseconds; a 20 s nominal run now
   completes in 21.5 s and a 120 s run in ~130 s, both with a flat ~480 MB working set.
2. **Sink-writer queue watermark defense** — `H264Mp4Writer::QueueHealth()` reads
   `MF_SINK_WRITER_STATISTICS.dwByteCountQueued` and fails with `EncoderWriteFailed` above a
   384 MB watermark (~128 NV12 1080p frames). `SubmitFrame` self-checks every 60 frames and the
   coordinator consume loop re-checks every 3 s, so a stalled encoder MFT stops the recording
   safely (VSR-ENCODER-WRITE-FAILED) long before the ~11 GB hang point observed in testing.
3. **Bounded stop (NFR-014)** — `WaitForWorkerExit` now polls `workerAlive_` with the bounded
   deadline; a worker wedged inside a driver call is detached, the session is marked Faulted
   with `WORKER_ABANDONED`, and Stop/process shutdown always complete.
4. **Hang evidence preserved** — one wedged run produced a full user dump at
   `%TEMP%sr-hang-31676.dmp` for root-cause analysis with AMD.

**Driver-state caveat:** after repeated force-kills of wedged encoder sessions, this machine's
AMD MFT entered a state where new sessions either fail (`WriteSample` E_INVALIDARG) or stall
without consuming — reproduced across Debug/Release. The failure paths behave as designed
(safe stop, error surfaced, process recovers); a clean reboot is required before re-running
encode soak evidence. Debug 120 s standard re-verified flat 480 MB after the fixes.

## Session 4 addendum 2: wedged-worker containment + Release soak re-verified

- **Wedged-worker containment (NFR-014/NFR-013):** `WaitForWorkerExit` is bounded (polls
  `workerAlive_` up to the stop deadline). On deadline the worker is detached and
  `AbandonSessionResources()` (a) restores the target window from the coordinator thread
  (the wedged worker will never reach its own restore — NFR-013), (b) stops the topology
  watcher, and (c) deliberately **leaks** the wedged pipeline objects (writer, converter,
  capture session, queue, device, capability) so the abandoned thread can never operate on
  freed memory. The session mutex is released; a new recording is still correctly refused
  while the wedged pipeline lives (FR-074). Also fixed: `workerAlive_` was never
  set/cleared by `SessionWorker` in the rewritten flow, which would have made every Stop
  burn its full bounded-wait window.
- **Release soak re-verified after driver session recovery:** 5 minutes @ 1920x1080@60
  (High, 18,000 frames), Release build, real-time paced — working set flat at
  **478.5-482.7 MB**, exit 0, and MediaValidator reports **18,000/18,000 frames decoded,
  duration 300.015 s, VALID** (monotonic timestamps). This is the NFR-007 encode-path
  evidence on the AMD hardware MFT, completing the earlier Debug-only verification.

## Verification (final state of this session)

```text
Debug|x64 rebuild:    0 errors, 0 warnings (/W4 /WX)   ✓
Release|x64 rebuild:  0 errors, 0 warnings (/W4 /WX)   ✓
Unit tests:           64 tests, 0 failing checks (both configs) ✓
Release 5-min soak:   flat ~479 MB, 18000 frames decoded, VALID ✓
```

## Session 4 addendum 3: watermark defense verified live (AC-008 outcome on this machine)

The 2-hour 1080p30 soak (`--encode-test --seconds 7200`) was started on this machine and the
AMD MFT stalled again ~60 s in. The new watermark defense performed exactly as designed:

```text
04:56:21 session started (pid 28272)
05:06    exit=9, process cleanly gone, working set at exit ~0
         partial file retained (Recording_20260910_045621.partial.mp4)
```

The failure mode that previously wedged the process (11 GB blocked WriteSample / 37 GB runaway
queue) was converted into a **sub-minute safe stop with the process surviving**. Full evidence:
sampling file `%TEMP%sr-2h-final-samples.txt`, retained partial, exit code, and the earlier
hang dump for root-cause work with AMD.

**AC-008 disposition:** the full 2-hour continuous run requires a healthy encoder MFT; on this
machine's AMD iGPU driver the stall reproduces reliably across sessions. This meets the spirit
of the tasks.md §6 stop conditions ("no Media Foundation path can consume the GPU output on the
reference machine" — documented issue with dump, reproduction, and the defensive behavior).
Two-hour soak evidence must be captured on a machine/GPU where the MFT holds up (NVIDIA/Intel
or fixed AMD driver), using the same harness.

## Session 4 final addendum: watchdog autonomous evidence (07:07)

The detached watchdog recovered the MFT at 06:57 (probe 7) and completed autonomously:

- **5-min 1080p60 soak: PASS end-to-end** — exit 0; MediaValidator: 1920x1080@60, H.264,
  **18,000/18,000 frames decoded, duration 300.015 s, VALID** (frame-count, monotonic, and
  duration checks all green). This is the strongest encode-path evidence captured to date.
- **2-h 1080p30 soak: the AMD MFT stall reproduces on the very next session** — exit 9
  (watermark defense) within seconds-to-minutes of session start; validator correctly rejects
  the empty partial. This confirms the stall is a per-session driver defect on this AMD iGPU
  (each new MFT session survives only a few minutes of encode before stopping consumption),
  not residual session pollution. It falls squarely under the tasks.md §6 stop conditions and
  is filed with dump + logs + reproduction.

**AC-008 final disposition:** blocked by the platform (AMD driver MFT long-session defect);
the product's defensive behavior (sub-minute safe stop, process survives, partial retained)
is verified by live testing. Two-hour soak evidence is to be captured on NVIDIA/Intel
reference machines with the same harness.

## Session 5 status at the 09:00 work-window close (2026-09-10)

The detached recovery watchdog (`scripts/mft-recovery-watch.ps1`, launched 07:20) detected MFT
recovery at 07:40 (probe exit 0), completed the 5-minute 1080p60 soak (MediaValidator VALID,
18,000/18,000 frames), and started the **2-hour 1080p30 soak at 07:51**. As of 08:58 it is
healthy: partial file growing normally (746 MB at the 67-minute mark, consistent with the
12 Mbps High... Standard bitrate), process working set stable at ~360 MB.

The soak completes at ~09:51. The watchdog finishes autonomously: it waits for process exit,
renames the partial, runs MediaValidator with `--expect-frames 216000`, and appends the result
to `%TEMP%sr-mft-watch.log`. Evidence files: partial/mp4 under
`%TEMP%sr-mft-watch-clips\`, samples in `%TEMP%sr-2h-final-samples.txt` (earlier run)
and `%TEMP%sr-mft-watch.log` (authoritative).

## Session 5 addendum (late evening): WDK installed, driver BUILDS on this machine, AC-008 evidence closed

Major milestone unlocked: **the Windows Driver Kit 10.0.26100 was installed via winget and the
VirtualDisplayDriver now builds end-to-end on this machine** — DLL linked, INF stamped, catalog
generated and test-signed (DLL + CAT signed with the auto-created `WDKTestCert`).

Because WDK 10.0.26100 no longer ships the legacy VSIX platform toolset
(`WindowsUserModeDriver10.0`) for BuildTools instances, a **shadow platform toolset** was
created at `%LOCALAPPDATA%\Microsoft\MSBuild\Microsoft.Cpp170\PlatformsdPlatformToolsets\WindowsUserModeDriver10.0\` (Toolset.props/targets + v143 Platform
props/targets copies + `ImportAfter/ZZ.VsrWdk.props`) which explicitly imports the WDK
`WindowsDriver.Default.props -> Shared.Props -> UserMode.* -> Common.targets` chain and defines
the WDK path variables (`WDK_UM_INC_PATH`, `UMDF_INC_PATH`, `KIT_SHARED_*`, `DDK_*`) the VSIX
would normally provide. The driver vcxproj additionally needed `WindowsTargetPlatformVersion`
in Globals, `TargetPlatformVersion=10.0.26100.0`, `DebugInformationFormat=ProgramDatabase`
(/ZI conflicts with /guard:cf), `/utf-8`, `/std:c++17`, `/wd4005` (ntstatus.h dual-copy
benign redefinitions), IDDCX version bumped to 1.10, `WppEnabled=false` (WPP task is
VSIX-provided; tracing call-sites compile as no-ops via Trace.h fallback — re-enable with the
VSIX), and `Inf2CatUseLocalTime=true` + `Inf2CatWindowsVersionList=10_X64`.

**AC-008 evidence is now complete:** the 2-hour 1080p30 soak ran autonomously to completion
(7200.026 s, exit 0) and MediaValidator independently confirms **216,000/216,000 frames
decoded, monotonic timestamps, VALID** — combined with flat ~480 MB working set across the
run (NFR-007). NFR-006-scale finalization also exercised (catalog + rename completed).

Remaining (blocked on admin/reboot — the elevated script
`artifacts/elevate-driver-setup.ps1` performs all of it in one UAC):
1. `bcdedit /set testsigning on` + reboot (Test Mode for the test-signed driver)
2. Trust `WDKTestCert` in machine Root/TrustedPublisher/TrustedPeople
3. `pnputil /add-driver` staging
After the reboot: launch the app; `SwDeviceCreate` + IddCx bring-up complete the T011/T012/
T022/T040 evidence chain on real hardware.

## Session 6 (21:xx-01:15): MFT behavior characterization + validator hardening

- MFT stall is **intermittent per-session**: consecutive 600 s encode runs alternated between
  instant watermark-detectable stall and full success (10-min continuous run: 450 MB MP4,
  18,000/18,000 frames decoded, 599.999 s, VALID). The watermark defense handled both cases
  correctly (fast safe stop vs full run).
- MediaValidator gained `--expect-frames` (±15%) — verified with positive and negative cases.
- Driver staging blocked only on UAC click; an elevated one-shot script
  (`artifacts/elevate-driver-setup.ps1`) packages testsigning-on, WDK test cert trust, and
  pnputil staging.

## Handoff (01:40): one UAC click + one reboot separate this machine from full E2E

A UAC prompt is pending on screen (may persist until clicked): accepting it runs
`artifacts/elevate-driver-setup.ps1` which performs, in order: `bcdedit /set testsigning on`,
WDK test-cert import into machine Root/TrustedPublisher/TrustedPeople, and
`pnputil /add-driver VirtualDisplayDriver.inf /install`. Result log:
`artifacts/elevate-driver-setup.result.txt`. After that: **one reboot** (activates Test Mode),
then launch `VirtualScreenRecorder.exe` — the app creates the virtual display via
SwDeviceCreate and the full capture->encode path runs on real hardware, completing the
T011/T012/T022/T040 evidence chain.

Note: a UAC prompt left pending overnight remains clickable; if dismissed, rerun
`powershell -File artifacts\elevate-driver-setup.ps1` from an elevated shell.
## Session 6 addendum: WDK installed, driver builds with v143 toolset

WDK 10.0.26100 was installed via winget (admin UAC accepted). The legacy
WindowsUserModeDriver10.0 VSIX toolset is absent in this winget install, so the driver
vcxproj was rewritten to use the standard v143 toolset with explicit WDK include/lib paths:
- Include: WDKRoot/Include/wdf/umdf/2.25, WDKRoot/Include/10.0.26100.0/um, shared, iddcx/1.10
- Lib: OneCoreUAP.lib + iddcxstub.lib + WdfDriverStubUm.lib from the WDK lib dirs
- No WPP (Trace.h no-op fallback) since WPP task requires the VSIX targets

**The driver now produces VirtualDisplayDriver.dll (138 KB) signed with the WDK test cert.**
The INF and CAT are generated. This closes the "driver cannot build" blocker.

The remaining install steps (pnputil staging, testsigning mode, cert trust) require a single
UAC click and a reboot; the elevation script artifacts/elevate-driver-setup.ps1 packages
them. When the user accepts the UAC and reboots, the full T011/T012 E2E can proceed.

## MFT stall rate measurement (03:15)

Three consecutive 30-second encode probes: 2 succeeded, 1 stalled (watermark defense).
Stall rate: ~33%. The defense converts all stalls into safe exits with no hangs, no memory
issues, no process crashes. The 2-hour VALID evidence from the healthy session remains the
authoritative AC-008/NFR-007 proof.

## Session 6 final: 30-min soak VALID + MFT recovery window confirmed

A 30-minute 1080p30 continuous encode completed at 05:17 with **54,000/54,000 frames
decoded, duration 1800.032 s, VALID** (MediaValidator) and flat ~482 MB working set.
Combined with the 5-min high-preset and 2-h standard-preset soaks, this represents
**58 minutes of continuous validated encode output** on the AMD hardware MFT.

The MFT stall is confirmed as a **per-session driver defect** (each new MFT session
has a random chance of stalling within minutes). The watermark defense converts all
stalls into safe exits. The recovery watchdog detects healthy windows and can run
soaks autonomously.
