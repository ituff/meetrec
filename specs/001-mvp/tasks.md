# Virtual Screen Recorder MVP Implementation Tasks

- **Document ID:** VSR-MVP-TASK-001
- **Version:** 1.0
- **Execution model:** Dependency-ordered, spec-driven
- **Priority:** P0 is required for MVP; P1 is desirable but may follow after the first accepted build.

## 1. Task execution protocol

For every task, Codex shall:

1. Read `spec.md`, `requirements.md`, `design.md`, and this task.
2. Confirm prerequisites are complete from repository evidence, not assumption.
3. Implement only the stated scope.
4. Add or update tests and documentation identified in the task.
5. Build the affected projects in `Debug|x64`.
6. Run relevant tests.
7. Record requirement IDs, files changed, commands run, results, limitations, and follow-up items in the task completion note.
8. Stop if a release-blocking architectural assumption is disproved and create an ADR or issue rather than silently changing architecture.

## 2. Milestones

```text
M0 Repository and toolchain
M1 Driver and virtual monitor proof
M2 Capture-to-MP4 vertical slice
M3 Target placement and recording orchestration
M4 UI, tray, diagnostics, and reliability
M5 Installer and release validation
```

## 3. Dependency graph

```text
T001 -> T002 -> T003
              |-> T010 -> T011 -> T012
              |-> T020 -> T021 -> T022 -> T023
              |-> T030 -> T031
T012 + T023 + T031 -> T040 -> T041 -> T042
T040 -> T050 -> T051
T041 -> T060 -> T061
T042 + T051 + T061 -> T070 -> T071 -> T072
T070 -> T080 -> T081 -> T082
T072 + T082 -> T090 -> T091 -> T092 -> T093
```

## 4. Detailed tasks

### T001 [P0] Create repository and specification baseline

**Requirements:** NFR-031, NFR-033, NFR-034, NFR-036  
**Dependencies:** None

**Work:**

- Create the repository structure defined in `design.md`.
- Add the four specification files under `specs/001-mvp/`.
- Add `.gitignore` rules for build outputs, recordings, logs, test certificates, keys, generated catalogs, and installer artifacts.
- Add an initial README containing scope, prerequisites, build placeholders, test-signing warning, and supported platform.
- Add ADR templates.

**Deliverables:**

- Repository skeleton
- Specification baseline
- README and ADR template

**Verification:**

- No secrets or generated binaries tracked.
- Paths and document links resolve.

**Done when:** Repository has a clean initial commit and all documents are internally linked.

---

### T002 [P0] Create Visual Studio solution and build conventions

**Requirements:** NFR-031, NFR-032, NFR-033, NFR-034  
**Dependencies:** T001

**Work:**

- Create `VirtualScreenRecorder.sln`.
- Create Win32 app, static library, test, and UMDF driver projects.
- Set x64-only configurations.
- Configure C++20, Unicode, conformance mode, warnings as errors for first-party app code, and common output directories.
- Add `Directory.Build.props` or equivalent shared configuration.
- Add empty compile-safe module entry points.

**Verification:**

- Entire solution builds in `Debug|x64` on a documented machine.
- Output follows the artifacts convention.

**Done when:** A clean checkout can compile all placeholder projects with one documented command.

---

### T003 [P0] Add deterministic build and test scripts

**Requirements:** NFR-036  
**Dependencies:** T002

**Work:**

- Implement `scripts/build.ps1`, `scripts/test.ps1`, and `scripts/clean.ps1`.
- Detect required Visual Studio, SDK, and WDK components.
- Fail with actionable messages when prerequisites are absent.
- Write a build manifest with tool versions and commit ID.

**Verification:**

- Build succeeds from Developer PowerShell and ordinary PowerShell when tools are installed.
- Missing WDK produces a clear non-zero failure.

**Done when:** CI-ready commands require no manual Visual Studio interaction.

---

### T010 [P0] Import and audit the IDD sample foundation

**Requirements:** FR-010, NFR-034, NFR-035  
**Dependencies:** T002

**Work:**

- Import only required Microsoft IDD sample concepts or source with attribution and license preservation.
- Produce a sample audit listing every imported file, TODO, unsupported feature, and planned removal.
- Rename identities and namespaces for Virtual Screen Recorder.
- Remove multi-monitor sample behavior and unrelated modes.
- Create `ADR-002-idd-driver-lifecycle.md` draft.

**Verification:**

- Driver project compiles.
- Static inspection confirms one intended monitor path.

**Done when:** The driver foundation is attributable, reviewed, and no sample TODO remains undocumented.

---

### T011 [P0] Implement one virtual monitor and required modes

**Requirements:** FR-010, FR-011, FR-012, NFR-011  
**Dependencies:** T010

**Work:**

- Initialize adapter and one monitor.
- Define 720p30, 1080p30, and 1080p60 modes.
- Mark 1080p30 preferred.
- Implement swap-chain assignment and release safely.
- Add WPP trace events for lifecycle callbacks.

**Verification:**

- Test-signed driver installs on a dedicated development machine.
- Windows Display Settings shows one additional extended monitor.
- All three modes are visible and selectable.
- Driver survives disable, enable, and reboot cycle in the test environment.

**Done when:** Evidence is captured in the driver manual-test log and no driver crash occurs.

---

### T012 [P0] Finalize driver lifecycle and stable identity spike

**Requirements:** FR-013, FR-014, FR-015, FR-016, FR-017, NFR-021  
**Dependencies:** T011

**Work:**

- Evaluate software-device creation versus persistent installed device.
- Implement the least fragile MVP lifecycle.
- Assign stable hardware and interface identities.
- Document how the desktop application discovers the monitor.
- Finalize ADR-002 with test evidence.
- Add install and remove development scripts using test signing.

**Verification:**

- Ten create or enable and disable cycles complete without stale duplicate monitors.
- Reboot retains a recoverable known state.
- Stable identity maps to the same logical virtual monitor independent of display ordinal.

**Done when:** The lifecycle decision is finalized and consumed by DisplayControl interfaces.

---

### T020 [P0] Implement D3D11 and WinRT graphics bootstrap

**Requirements:** FR-041, FR-042, NFR-010, NFR-012  
**Dependencies:** T002

**Work:**

- Initialize COM, WinRT, Media Foundation lifecycle placeholder, and a BGRA-compatible D3D11 device.
- Add adapters that bridge DXGI and WinRT Direct3D objects.
- Detect and map device-creation failures.
- Add RAII wrappers and unit-testable error mappings.

**Verification:**

- A diagnostic executable or test reports selected adapter and feature level.
- Forced invalid-adapter path returns `VSR-INIT-D3D-FAILED` without leaking resources.

**Done when:** A reusable graphics device service is available to capture and encoder modules.

---

### T021 [P0] Implement virtual monitor discovery

**Requirements:** FR-013, FR-014, FR-040  
**Dependencies:** T012, T020

**Work:**

- Enumerate display paths and map stable driver identity to monitor descriptor and `HMONITOR`.
- Return bounds, work area, DPI, active state, and current mode.
- Subscribe to display topology changes.
- Add a diagnostic command that prints sanitized topology.

**Verification:**

- Discovery succeeds regardless of whether the virtual monitor is enumerated as DISPLAY2, DISPLAY3, or later.
- Disconnect produces an observable topology event.

**Done when:** Capture code can obtain the virtual monitor without fixed names or indices.

---

### T022 [P0] Create monitor capture vertical spike

**Requirements:** FR-040, FR-041, FR-042, FR-047  
**Dependencies:** T020, T021

**Work:**

- Create a monitor-based `GraphicsCaptureItem`.
- Start `Direct3D11CaptureFramePool` and `GraphicsCaptureSession`.
- Receive frames for at least 60 seconds.
- Log frame dimensions and rate without writing pixels to disk.
- Verify operation while the spike's window is minimized.
- Create ADR-003.

**Verification:**

- Frames continue for 60 seconds with controller minimized.
- Physical-monitor activity is absent from virtual-monitor frame checks.
- Capture stops cleanly.

**Done when:** The monitor capture assumption is proven on the reference machine.

---

### T023 [P0] Implement bounded frame ownership and queue

**Requirements:** FR-043, FR-044, FR-045, FR-046, NFR-007  
**Dependencies:** T022

**Work:**

- Implement a three-frame bounded queue.
- Establish safe texture ownership through copied pool textures or proven frame retention.
- Add captured, queued, encoded, and dropped counters.
- Add cancellation and queue completion semantics.
- Create ADR-005.

**Verification:**

- Unit tests cover normal enqueue, overflow eviction, shutdown, and concurrent producer or consumer behavior.
- A 30-minute capture-only soak shows bounded memory.

**Done when:** The encoder can consume stable textures without blocking the capture callback.

---

### T030 [P0] Implement Media Foundation encoder capability probe

**Requirements:** FR-050, FR-051, FR-062, FR-063  
**Dependencies:** T020

**Work:**

- Enumerate compatible H.264 encoder transforms.
- Prefer hardware and D3D11-aware transforms.
- Report selected transform, accepted input formats, and supported presets.
- Return `VSR-ENCODER-UNAVAILABLE` if no compliant path exists.
- Create ADR-004 draft.

**Verification:**

- Probe output is recorded on available Intel, NVIDIA, or AMD test devices.
- Unsupported path fails before an output file is created.

**Done when:** The pipeline has a deterministic encoder selection policy.

---

### T031 [P0] Implement GPU format conversion and MP4 sink

**Requirements:** FR-050 through FR-060, NFR-005, NFR-006  
**Dependencies:** T030

**Work:**

- Implement direct-texture input when supported.
- Implement D3D11 conversion to NV12 when required.
- Configure Media Foundation output for all presets.
- Write samples to `.partial.mp4`.
- Drain, finalize, close, and rename on success.
- Preserve partial output and error details on failure.
- Finalize ADR-004.

**Verification:**

- Encode generated GPU test frames for 10 seconds in each preset.
- Resulting MP4 is playable and matches expected dimensions and duration.
- No CPU readback occurs in normal path.

**Done when:** Encoder accepts queued D3D textures and produces validated MP4 output.

---

### T040 [P0] Integrate capture-to-MP4 vertical slice

**Requirements:** FR-040 through FR-063, NFR-005, NFR-006, NFR-007  
**Dependencies:** T023, T031

**Work:**

- Connect virtual monitor capture, bounded queue, conversion, and encoder.
- Implement monotonic timestamps and nominal frame durations.
- Add command-line development entry point to record for a fixed duration.
- Collect pipeline performance counters.

**Verification:**

- Record the virtual display for 5 minutes at Standard preset.
- File plays end-to-end, has monotonic timestamps, and duration is within tolerance.
- Memory remains bounded.

**Done when:** A non-UI end-to-end recording succeeds repeatedly.

---

### T041 [P0] Implement pause, resume, stop, and finalization

**Requirements:** FR-055 through FR-058, FR-070 through FR-074, NFR-014  
**Dependencies:** T040

**Work:**

- Add idempotent Stop.
- Add Pause and Resume with accumulated pause duration.
- Clear stale frames at pause boundaries.
- Serialize commands and reject invalid transitions.
- Add forced error-path finalization attempts.

**Verification:**

- Automated timestamp tests cover multiple pause intervals.
- Integration test records 10 seconds, pauses 10, resumes 10, and validates approximately 20 seconds output.
- Repeated Stop does not deadlock or double-finalize.

**Done when:** AC-005 passes.

---

### T042 [P0] Implement recording session state machine

**Requirements:** FR-070 through FR-077, NFR-012, NFR-014  
**Dependencies:** T040, T041

**Work:**

- Implement all states and transitions from `spec.md`.
- Define typed commands and immutable status snapshots.
- Map module failures to stable error codes.
- Generate a per-session correlation ID.
- Add unit tests for every allowed and rejected transition.

**Verification:**

- State transition tests achieve complete transition-branch coverage.
- No invalid command reaches a subsystem.

**Done when:** Coordinator is the sole owner of recording lifecycle.

---

### T050 [P0] Implement window enumeration and selection model

**Requirements:** FR-020, FR-021, FR-022, FR-030  
**Dependencies:** T040

**Work:**

- Enumerate eligible top-level windows.
- Resolve executable name and icon safely.
- Exclude recorder and unusable windows.
- Refresh without invalidating a confirmed selection unexpectedly.
- Detect stale HWND reuse by validating PID.

**Verification:**

- Manual test covers Win32, Chromium-based, and packaged applications.
- Closed windows disappear without crashes.

**Done when:** UI-independent selection model returns stable target descriptors.

---

### T051 [P0] Implement target move, monitor, and restore

**Requirements:** FR-023 through FR-029, NFR-013, NFR-040  
**Dependencies:** T050, T021

**Work:**

- Capture original placement and monitor.
- Move normal and maximized windows to virtual work area.
- Verify destination monitor.
- Monitor target destruction.
- Restore original location or a visible primary-monitor fallback.
- Add geometry and state unit tests.

**Verification:**

- AC-004 and AC-006 pass.
- Mixed-DPI manual placement test passes.

**Done when:** Target window never remains intentionally inaccessible after handled stop or failure.

---

### T060 [P0] Implement settings, output validation, and disk guard

**Requirements:** FR-052 through FR-060, FR-090 through FR-092, NFR-024  
**Dependencies:** T041

**Work:**

- Implement settings schema, defaults, loading, validation, and atomic save.
- Implement output directory picker integration interface.
- Generate collision-safe names.
- Check writability and free space before start.
- Poll free space during recording using a documented threshold.

**Verification:**

- Unit tests cover invalid JSON, unknown fields, Unicode paths, unwritable directories, collisions, and low-space condition.

**Done when:** Output failures are detected before or safely during recording.

---

### T061 [P0] Implement structured logging and diagnostics

**Requirements:** FR-006, FR-093 through FR-096  
**Dependencies:** T060

**Work:**

- Implement structured log records and rotation.
- Add correlation IDs and stable event IDs.
- Redact sensitive fields and omit captured content.
- Implement diagnostics package creation with sanitized metadata.

**Verification:**

- Tests verify rotation and redaction.
- Diagnostics package contains no video frames, tokens, passwords, or raw environment values.

**Done when:** Every terminal error path emits an actionable correlated record.

---

### T070 [P0] Build the Win32 controller UI

**Requirements:** FR-003, FR-070, FR-075, FR-076, FR-077, NFR-040 through NFR-044  
**Dependencies:** T042, T051, T061

**Work:**

- Build native main window and controls.
- Bind application list, output directory, preset, cursor option, and commands.
- Display state, duration, FPS, dropped frames, and error details.
- Disable invalid commands based on coordinator snapshot.
- Add keyboard navigation and accessible labels.
- Declare Per-Monitor V2 DPI awareness.

**Verification:**

- All primary flows can be completed using keyboard only.
- UI remains responsive during recording and finalization.
- DPI tests at 100%, 150%, and 200% show no clipped primary controls.

**Done when:** A user can complete the primary journey without command-line tools.

---

### T071 [P0] Implement tray lifecycle and single-instance activation

**Requirements:** FR-005, FR-080 through FR-085  
**Dependencies:** T070

**Work:**

- Add single-instance mutex and activation mechanism.
- Add tray icon, tooltip, state variants, and menu.
- Hide controller on close during recording.
- Route Pause, Resume, Stop, Open, and Exit through coordinator.
- Ensure Explorer restart recreates the tray icon.

**Verification:**

- AC-003 and AC-009 pass.
- Explorer restart test restores tray access.

**Done when:** Recording is fully controllable while the main window is hidden.

---

### T072 [P0] Implement preview renderer

**Requirements:** FR-047, FR-048  
**Dependencies:** T070

**Work:**

- Add optional preview using sampled frames.
- Limit preview rate independently of recording rate.
- Release preview resources when hidden.
- Ensure no dependency from capture lifetime to preview-window lifetime.

**Verification:**

- Open, close, minimize, and restore preview repeatedly during a 10-minute recording.
- Output continuity is unaffected.

**Done when:** Preview is observability-only and cannot terminate recording.

---

### T080 [P0] Integrate end-to-end application flow

**Requirements:** FR-001 through FR-096 as applicable  
**Dependencies:** T071, T072

**Work:**

- Wire startup checks, driver discovery, target selection, placement, capture, encoder, tray, restore, and diagnostics.
- Ensure rollback ordering for every Preparing-stage failure.
- Add user-facing limitation text.
- Implement controlled shutdown.

**Verification:**

- AC-002 through AC-009 pass on the primary reference machine.

**Done when:** The complete primary user journey works from the installed debug or internal build.

---

### T081 [P0] Harden failure and recovery paths

**Requirements:** FR-015, FR-028, FR-060, NFR-012, NFR-013, NFR-014, NFR-043  
**Dependencies:** T080

**Work:**

- Inject target loss, monitor loss, D3D device loss where feasible, disk low, write failure, encoder failure, and repeated shutdown.
- Verify bounded waits and no deadlocks.
- Ensure target restoration fallback.
- Improve actionable error messages and stable codes.

**Verification:**

- Failure-injection checklist is complete.
- App returns to Ready or Faulted without orphan recording state.

**Done when:** No tested recoverable failure requires Task Manager to exit the app.

---

### T082 [P0] Add automated and manual acceptance harness

**Requirements:** All MUST verification requirements  
**Dependencies:** T081

**Work:**

- Add unit-test runner integration.
- Add Windows integration-test scripts.
- Add media validation script or documented test utility.
- Create manual test checklist with expected evidence.
- Add requirement-to-test traceability matrix as a Markdown document.

**Verification:**

- Each MUST requirement maps to at least one test or inspection.
- Test report is generated under artifacts without committing recordings.

**Done when:** Release qualification is repeatable by a second developer.

---

### T090 [P0] Create internal test installer

**Requirements:** FR-001, FR-002, FR-003, NFR-020, NFR-022, NFR-023  
**Dependencies:** T082

**Work:**

- Create Inno Setup project.
- Package app and test-signed driver for internal use.
- Add preflight checks, install log, driver install and removal, shortcuts, upgrade process, and running-app handling.
- Preserve recordings and settings on uninstall.
- Clearly label test-signed build limitations.

**Verification:**

- Clean install and uninstall succeed on a disposable test machine.
- App runs unelevated after install.
- Uninstall does not delete recordings.

**Done when:** Internal users receive one Setup executable and documented Test Mode prerequisites.

---

### T091 [P0] Performance and soak validation

**Requirements:** NFR-001 through NFR-007  
**Dependencies:** T090

**Work:**

- Measure package size, startup time, CPU, private working set, GPU engine use, frame counts, and finalization time.
- Run 30-minute and two-hour recordings.
- Profile hot paths and remove avoidable copies or wakeups.
- Record the reference machine specification.

**Verification:**

- All MUST performance requirements pass.
- SHOULD misses are documented with measurements and improvement plan.

**Done when:** Signed-off performance report is stored with build artifacts.

---

### T092 [P0] Cross-machine compatibility validation

**Requirements:** NFR-030, NFR-040, release success metrics  
**Dependencies:** T091

**Work:**

- Test at least two Windows 11 x64 machines and two GPU vendors where available.
- Test Win32, Chromium-based, and Office-style targets.
- Test common DPI combinations and display topology changes.
- Record unsupported environments and reproducible defects.

**Verification:**

- Critical primary journey passes on both machines.
- No open Critical or High severity defect remains.

**Done when:** Compatibility report and known limitations are approved.

---

### T093 [P0] Prepare distributable MVP release gate

**Requirements:** NFR-021, NFR-022, AC-001, AC-010  
**Dependencies:** T092

**Work:**

- Integrate production signing hooks without storing secrets.
- Produce signed application, driver catalog, and installer.
- Validate on clean Windows 11 x64 with standard signing enforcement and Secure Boot where available.
- Test install, launch, upgrade, repair or reinstall, and uninstall.
- Add release notes, checksums, version metadata, and known limitations.

**Verification:**

- No Test Mode required.
- Windows accepts the driver and installer signatures.
- AC-001 and AC-010 pass.

**Done when:** One signed setup executable is approved for MVP distribution.

## 5. P1 backlog

### T100 [P1] Improve owned-popup relocation

Track new top-level windows from the target process, distinguish modal dialogs from unrelated windows, and relocate them safely without stealing unrelated focus.

### T101 [P1] Add cursor capture preference

Expose cursor capture when supported and verify cursor behavior across monitor capture and preview.

### T102 [P1] Add optional system audio design spike

Evaluate WASAPI loopback, synchronization, AAC encoding, device changes, and privacy UX. Do not implement audio until the spike and spec update are approved.

### T103 [P1] Add crash-report metadata

Add local opt-in crash dumps with strict privacy defaults and diagnostics packaging. No automatic upload in MVP.

### T104 [P1] Add MSI or enterprise installer assessment

Evaluate WiX/MSI, Intune, SCCM, silent deployment, repair, and driver-upgrade behavior.

## 6. Global stop conditions

Codex shall stop the current implementation sequence and raise a documented decision when any of these occurs:

- The selected Windows API cannot capture the IDD monitor.
- The selected driver lifecycle creates duplicate or stale monitors after restart.
- No Media Foundation hardware encoder can consume the GPU path on the reference machine.
- The only working path requires CPU readback for every frame.
- A requirement would require bypassing DRM or Windows capture restrictions.
- Production driver distribution cannot be achieved within project constraints.
- A requested change expands MVP into audio, streaming, editing, multiple monitors, or cross-platform support.

The issue shall include observed evidence, affected requirement IDs, alternatives, and a recommended ADR or spec change.

## 7. MVP completion checklist

- [ ] All P0 tasks complete
- [ ] All MUST requirements traced and verified
- [ ] Build reproducible from clean environment
- [ ] Driver installation validated under target signing policy
- [ ] 30-minute and two-hour recordings validated
- [ ] Controller minimization does not affect output
- [ ] Physical desktop activity is excluded
- [ ] Target restoration passes normal and failure paths
- [ ] Install, upgrade, and uninstall pass
- [ ] No Critical or High defects open
- [ ] README, release notes, diagnostics guide, and known limitations complete
- [ ] Final signed setup artifact and checksums produced
