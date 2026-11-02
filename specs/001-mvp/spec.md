# Virtual Screen Recorder MVP Specification

- **Spec ID:** VSR-MVP-001
- **Version:** 1.0
- **Status:** Ready for implementation
- **Target platform:** Windows 11 x64
- **Primary audience:** Codex and human reviewers
- **Architecture baseline:** Native C++20, Win32, C++/WinRT, Windows.Graphics.Capture, Direct3D 11, Media Foundation, UMDF 2/IddCx

## 1. Product summary

Virtual Screen Recorder is a lightweight Windows desktop application that creates one virtual extended display, moves a selected application window to that display, and records the virtual display to an H.264 MP4 file. The application's control window can be minimized or hidden in the system tray without interrupting recording.

The target application must remain in a normal rendering state on the virtual display. The product does not promise reliable capture when the target application itself is minimized, suspended, protected by DRM, or rendered through an unsupported graphics path.

## 2. Problem statement

Normal desktop recording is easily affected by notifications, unrelated windows, mouse activity, and task switching on the physical display. Window-only capture may also fail or freeze for applications that stop rendering while minimized. The product shall isolate the recording workload on a Windows-recognized virtual display while allowing the user to continue working on the physical display.

## 3. Goals

1. Provide a single-install Windows MVP that creates one virtual display.
2. Let the user select a running top-level application window.
3. Move and resize that window onto the virtual display.
4. Record the complete virtual display to a valid MP4 file.
5. Continue recording while the controller is minimized or hidden in the tray.
6. Restore the selected window after recording stops.
7. Remain lightweight by using Windows-native APIs and codecs.
8. Fail safely and produce actionable diagnostics.

## 4. Non-goals

The MVP shall not implement:

- Multi-monitor virtual display creation
- macOS or Linux support
- Windows 10 certification
- 4K, HDR, H.265, AV1, or variable frame rate
- Streaming, cloud upload, remote control, or collaboration
- Video editing, annotations, overlays, webcam, watermark, or scene composition
- Multi-track audio or microphone mixing
- DRM or protected-content bypass
- Capture across secure desktop, UAC consent desktop, sign-out, or lock screen
- A Windows service or unattended multi-user session recorder
- Automatic application installation or launching
- Recording an application that deliberately suppresses capture

## 5. Personas

### 5.1 Primary user

A Windows user who needs to record a demonstration, browser workflow, presentation, or business application without unrelated physical-desktop activity appearing in the video.

### 5.2 Maintainer

A developer or IT administrator who builds, signs, installs, diagnoses, and supports the application and virtual display driver.

## 6. Primary user journey

1. The user installs the signed setup package with administrator elevation.
2. The installer installs the desktop application and virtual display driver.
3. The user launches Virtual Screen Recorder.
4. The application verifies the driver and creates or discovers the virtual display.
5. The user selects a running application window.
6. The user chooses an output folder and quality preset.
7. The user selects **Start Recording**.
8. The application saves the original window placement, moves the window to the virtual display, initializes capture and encoding, and enters Recording state.
9. The user minimizes or closes the controller to the tray and continues using the physical display.
10. The user selects **Stop Recording** from the controller or tray.
11. The application finalizes the MP4, restores the target window, and shows the output path.

## 7. MVP capabilities

### 7.1 Virtual display

- Exactly one virtual monitor named `Virtual Recording Display`.
- Supported display modes:
  - 1280 x 720 at 30 Hz
  - 1920 x 1080 at 30 Hz, preferred
  - 1920 x 1080 at 60 Hz
- SDR, 8-bit output only.
- The application identifies the display by stable driver or monitor identity, not by screen index.
- The virtual display remains connected while recording.

### 7.2 Window selection and placement

- List visible, unowned top-level windows with non-empty titles.
- Exclude the recorder itself, shell surfaces, cloaked windows, and tool windows where practical.
- Store process ID, HWND, title, executable name, window placement, restore rectangle, show state, and source monitor.
- Move the selected window inside the virtual monitor work area.
- Restore the window after a normal stop or recoverable failure.
- Detect target-window destruction and stop recording safely.

### 7.3 Recording

- Capture the virtual display, not the preview window.
- Default output: H.264 video in MP4, 1920 x 1080, 30 FPS.
- Rate control: constant quality (CRF) with a per-preset bitrate ceiling and a
  5-second keyframe interval. Screen and meeting content is mostly static, so a
  bitrate target over-spends on unchanged frames, and a short keyframe interval
  re-codes whole frames at full quality; both made earlier recordings large.
  Reference benchmark: a Teams cloud recording of comparable content (1080p,
  16 FPS, 326 kbps video, 64 kbps mono AAC) is 74 MB for 25 minutes. Standard
  meets or beats that size while keeping on-screen text legible.
- Presets (bitrate ceiling / audio track):
  - Low: 1280 x 720, 30 FPS, CRF 28, 1 Mbps, 48 kbps mono AAC
  - Standard: 1920 x 1080, 30 FPS, CRF 26, 2 Mbps, 64 kbps mono AAC
  - High: 1920 x 1080, 60 FPS, CRF 22, 6 Mbps, 128 kbps stereo AAC
- File name pattern: `Recording_yyyyMMdd_HHmmss.mp4`.
- Support Start, Pause, Resume, and Stop.
- A paused interval shall not create an equivalent-duration frozen segment in the final output.
- Finalize the MP4 on Stop and on controlled application shutdown.
- Use hardware H.264 encoding when available, with a clear error or documented fallback behavior when unavailable.
  Hardware and Media Foundation encoders have no constant-quality mode; they
  target the preset bitrate (ABR) instead.

### 7.4 Controller and tray

- Native Win32 controller window.
- Display driver state, virtual display state, selected target, output path, preset, recording duration, approximated FPS, dropped frames, and latest error.
- Closing the controller during recording hides it to the tray.
- Tray commands: Open, Pause or Resume, Stop, Exit.
- Exit while recording requires an explicit in-app decision to stop and finalize. Silent process termination is not permitted.

### 7.5 Configuration and logs

- Store user settings under `%LOCALAPPDATA%\VirtualScreenRecorder\settings.json`.
- Store logs under `%LOCALAPPDATA%\VirtualScreenRecorder\Logs`.
- Do not log captured pixels, credentials, document contents, or full command lines of target applications.
- Rotate logs by size or age.
- Provide a diagnostics command that packages logs and non-sensitive runtime metadata.

## 8. State model

Allowed states:

- `Initializing`
- `Ready`
- `Preparing`
- `Recording`
- `Paused`
- `Stopping`
- `Faulted`
- `ShuttingDown`

Allowed transitions:

```text
Initializing -> Ready | Faulted
Ready -> Preparing | ShuttingDown
Preparing -> Recording | Ready | Faulted
Recording -> Paused | Stopping | Faulted
Paused -> Recording | Stopping | Faulted
Stopping -> Ready | Faulted
Faulted -> Ready | ShuttingDown
Ready -> ShuttingDown
```

All commands that are invalid for the current state shall be disabled in the UI and rejected by the application layer.

## 9. Success metrics

The MVP is successful when all of the following are demonstrated on the supported test matrix:

- Setup installs and launches successfully on a clean Windows 11 x64 test machine.
- One virtual display appears as an extended monitor.
- A selected application can be moved to the virtual display and restored.
- A 30-minute 1080p30 recording completes and plays in Windows Media Player.
- Physical-display window switching does not appear in the recorded virtual-display video.
- Minimizing or hiding the controller does not stop capture.
- The resulting MP4 has monotonic timestamps and duration within 2 seconds of expected active recording time.
- The application does not crash during the defined acceptance test suite.

## 10. Quality targets

- Installed application and driver payload target: 20 MB or less, excluding symbols and installer overhead.
- Idle private working set target: less than 60 MB.
- 1080p30 recording private working set target: less than 250 MB.
- Idle CPU target: less than 1% on the reference machine.
- Hardware-encoded 1080p30 CPU target: less than 15% average on the reference machine.
- Start recording within 5 seconds after command acceptance.
- Stop and finalize a 30-minute recording within 10 seconds on local SSD.
- No unbounded frame queue. Backpressure must drop or coalesce frames instead of exhausting memory.

## 11. Supported environment

- Windows 11 x64, versions still supported by Microsoft at release time.
- Interactive local desktop session.
- Direct3D 11 capable GPU and driver.
- Local NTFS output target for acceptance testing.
- Standard DPI and mixed-DPI environments, with the application declared Per-Monitor V2 DPI aware.

Remote Desktop, Windows Sandbox, nested virtualization, GPU-less servers, third-party display mirroring software, and enterprise security products may affect behavior and are outside the guaranteed MVP support boundary.

## 12. Security and privacy

- Driver and installer must be signed for external distribution.
- Elevation is allowed only during install, repair, and uninstall.
- The desktop application runs as the standard interactive user.
- Output is written only to the user-selected local path.
- The application must not bypass capture restrictions or protected media controls.
- Validate all paths and avoid loading DLLs from writable search locations.
- Use safe defaults and explicit error messages for overwrite, insufficient storage, and permission failures.

## 13. Release gates

### Internal MVP

- Test-signed driver is acceptable.
- Test Mode may be enabled on dedicated development machines.
- Manual driver installation script is acceptable.

### Distributable MVP

- Production-compatible driver signing is complete.
- Single setup executable installs and removes the app and driver.
- No Test Mode requirement.
- Clean install, upgrade, repair, and uninstall tests pass.

## 14. Definition of done

The MVP is done when:

1. All `MUST` requirements in `requirements.md` are implemented.
2. All P0 tasks in `tasks.md` are complete.
3. Automated unit and integration tests pass.
4. Manual acceptance scenarios pass on at least two representative Windows 11 x64 machines with different GPU vendors where available.
5. Known limitations are documented in README and release notes.
6. No open Critical or High severity defect remains.
7. The build is reproducible from a clean documented environment.
