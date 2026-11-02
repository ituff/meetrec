# Virtual Screen Recorder MVP Requirements

- **Document ID:** VSR-MVP-REQ-001
- **Version:** 1.0
- **Normative terms:** MUST, SHOULD, MAY follow their usual requirements meaning.

## 1. Functional requirements

### 1.1 Installation and startup

- **FR-001 [MUST]** The product shall provide an x64 installer that installs the desktop application and the IDD driver.
- **FR-002 [MUST]** Installation and driver removal shall request administrator elevation.
- **FR-003 [MUST]** The application shall run without administrator rights after installation.
- **FR-004 [MUST]** Startup shall verify OS version, architecture, application single-instance ownership, driver presence, and required graphics capabilities.
- **FR-005 [MUST]** A second application launch shall activate the existing instance and exit.
- **FR-006 [MUST]** Startup failures shall show a user-actionable message and record a diagnostic error code.
- **FR-007 [SHOULD]** The installer shall support silent install and uninstall switches for testing and future enterprise deployment.

### 1.2 Virtual display lifecycle

- **FR-010 [MUST]** The driver shall expose exactly one indirect display adapter and one virtual monitor for the MVP.
- **FR-011 [MUST]** The monitor shall expose 1280x720@30, 1920x1080@30, and 1920x1080@60 modes.
- **FR-012 [MUST]** 1920x1080@30 shall be the preferred default mode.
- **FR-013 [MUST]** The application shall locate the monitor by a stable device identity.
- **FR-014 [MUST]** The application shall verify the monitor is active and part of the extended desktop before recording.
- **FR-015 [MUST]** A virtual monitor interruption during recording shall trigger a safe recording stop and MP4 finalization attempt.
- **FR-016 [SHOULD]** The UI shall allow enabling or disabling the virtual monitor when recording is inactive.
- **FR-017 [MUST]** Disabling the virtual monitor shall be rejected while Preparing, Recording, Paused, or Stopping.

### 1.3 Window discovery and placement

- **FR-020 [MUST]** The application shall enumerate eligible top-level windows in the current interactive session.
- **FR-021 [MUST]** Each list item shall show application icon where available, executable name, and window title.
- **FR-022 [MUST]** The application shall exclude its own windows and ineligible shell or cloaked windows where detectable.
- **FR-023 [MUST]** Before moving a window, the application shall persist its original `WINDOWPLACEMENT`, monitor identity, and rectangle in memory.
- **FR-024 [MUST]** The application shall move and resize the selected target wholly inside the virtual monitor work area.
- **FR-025 [MUST]** A maximized target shall be restored, moved, and then maximized on the virtual monitor.
- **FR-026 [MUST]** On Stop or recoverable initialization failure, the application shall attempt to restore the original placement.
- **FR-027 [MUST]** If the original monitor is unavailable, the application shall restore the window visibly on the current primary monitor.
- **FR-028 [MUST]** If the target window is destroyed during recording, the application shall stop with error `VSR-TARGET-LOST`.
- **FR-029 [SHOULD]** The application shall detect new owned top-level windows from the target process and offer or apply placement on the virtual display.
- **FR-030 [MUST]** The UI shall state that minimizing the target application may freeze or suppress its rendering.

### 1.4 Capture

- **FR-040 [MUST]** Capture shall target the virtual monitor, not the controller, preview, or target HWND.
- **FR-041 [MUST]** Capture shall use a GPU-backed Direct3D 11 frame path.
- **FR-042 [MUST]** Capture initialization shall verify frame size and pixel format.
- **FR-043 [MUST]** The pipeline shall tolerate duplicate, delayed, or dropped input frames without producing non-monotonic output timestamps.
- **FR-044 [MUST]** The frame queue shall have a fixed upper bound.
- **FR-045 [MUST]** When the queue is full, the pipeline shall drop the oldest unencoded frame or apply a documented coalescing policy.
- **FR-046 [MUST]** The application shall count captured, encoded, and dropped frames.
- **FR-047 [MUST]** Capture shall continue when the controller is hidden or minimized.
- **FR-048 [MUST]** Closing the preview shall not terminate capture.
- **FR-049 [SHOULD]** The user shall be able to exclude the pointer from capture when supported.

### 1.5 Encoding and files

- **FR-050 [MUST]** Output shall be H.264 video in an MP4 container.
- **FR-051 [MUST]** The application shall implement Low, Standard, and High presets defined in `spec.md`.
- **FR-052 [MUST]** The output path shall be selected before recording and remembered for the next run.
- **FR-053 [MUST]** The default output name shall follow `Recording_yyyyMMdd_HHmmss.mp4` using local time.
- **FR-054 [MUST]** The application shall not overwrite an existing file without explicit authorization.
- **FR-055 [MUST]** Output timestamps shall be monotonic and shall exclude paused duration.
- **FR-056 [MUST]** Stop shall flush pending samples, finalize the sink, close file handles, and report finalization success or failure.
- **FR-057 [MUST]** A partially produced file shall use a temporary `.partial.mp4` suffix until finalization succeeds.
- **FR-058 [MUST]** After successful finalization, the application shall atomically rename the temporary file to `.mp4` where the file system permits.
- **FR-059 [MUST]** The program shall check output directory writability and available disk space before starting.
- **FR-060 [MUST]** The application shall stop safely if a write failure or critically low disk condition occurs.
- **FR-061 [MUST]** No audio track is required in MVP 1.0.
- **FR-062 [SHOULD]** The encoder shall prefer a hardware Media Foundation transform.
- **FR-063 [MUST]** If no supported encoder can be activated, the application shall fail before recording and show `VSR-ENCODER-UNAVAILABLE`.

### 1.6 Recording controls

- **FR-070 [MUST]** Start shall be available only in Ready state with a valid target, virtual monitor, and output path.
- **FR-071 [MUST]** Pause shall stop submitting new output samples without destroying the capture session.
- **FR-072 [MUST]** Resume shall continue with timestamps adjusted to remove paused duration.
- **FR-073 [MUST]** Stop shall be idempotent.
- **FR-074 [MUST]** Only one recording session may exist per user session.
- **FR-075 [MUST]** UI commands shall remain responsive while capture and encoding run.
- **FR-076 [MUST]** The application shall display elapsed active recording time.
- **FR-077 [MUST]** The application shall display current state and the most recent failure.

### 1.7 Tray behavior

- **FR-080 [MUST]** The application shall create a notification-area icon while running.
- **FR-081 [MUST]** During recording, closing the controller shall hide it instead of exiting.
- **FR-082 [MUST]** The tray menu shall provide Open, Pause or Resume, Stop, and Exit commands appropriate to current state.
- **FR-083 [MUST]** The tray tooltip shall show recording state and elapsed time while recording.
- **FR-084 [MUST]** Exit during recording shall present an application confirmation flow that finalizes the recording before process exit.
- **FR-085 [SHOULD]** The tray icon shall visually distinguish Ready, Recording, Paused, and Faulted states.

### 1.8 Settings and diagnostics

- **FR-090 [MUST]** Settings shall be stored per user in UTF-8 JSON.
- **FR-091 [MUST]** Unknown settings fields shall be ignored for forward compatibility.
- **FR-092 [MUST]** Invalid settings shall fall back to defaults and generate a warning log.
- **FR-093 [MUST]** Logs shall include UTC timestamp, severity, component, event ID, correlation ID, and message.
- **FR-094 [MUST]** Logs shall not contain captured image content, document content, passwords, access tokens, or environment-variable values.
- **FR-095 [MUST]** Logs shall rotate and retain no more than seven days or 50 MB by default.
- **FR-096 [SHOULD]** A diagnostics package shall include logs, app version, OS build, GPU description, display topology, driver version, and sanitized settings.

## 2. Non-functional requirements

### 2.1 Performance

- **NFR-001 [MUST]** Idle average CPU usage shall remain below 1% on the reference machine after a 60-second settling period.
- **NFR-002 [SHOULD]** Idle private working set shall remain below 60 MB.
- **NFR-003 [SHOULD]** Recording private working set shall remain below 250 MB at 1080p30.
- **NFR-004 [SHOULD]** Average CPU shall remain below 15% during hardware-encoded 1080p30 recording on the reference machine.
- **NFR-005 [MUST]** Start shall reach Recording or a terminal error within 5 seconds.
- **NFR-006 [MUST]** Stop shall finalize within 10 seconds for a 30-minute local-SSD recording under normal conditions.
- **NFR-007 [MUST]** The process shall not exhibit unbounded memory growth during a two-hour soak test.

### 2.2 Reliability

- **NFR-010 [MUST]** The application shall use RAII for COM, WinRT, handles, D3D objects, and file resources.
- **NFR-011 [MUST]** Exceptions shall not cross Win32 callback or driver callback boundaries.
- **NFR-012 [MUST]** Device-lost, display-removed, target-lost, and disk-write failures shall enter a controlled stopping or fault path.
- **NFR-013 [MUST]** The application shall never intentionally leave the target window off-screen after a handled failure.
- **NFR-014 [MUST]** Repeated Stop or shutdown notifications shall not double-finalize or deadlock.

### 2.3 Security

- **NFR-020 [MUST]** The application shall run least-privileged after installation.
- **NFR-021 [MUST]** Driver installation shall use a signed catalog for distributable builds.
- **NFR-022 [MUST]** Build and installer scripts shall not embed signing passwords, private keys, or secrets.
- **NFR-023 [MUST]** DLL loading shall use controlled paths and safe search flags.
- **NFR-024 [MUST]** User-controlled paths shall be validated and handled with Unicode Win32 APIs.
- **NFR-025 [MUST]** The product shall not attempt to bypass DRM, capture exclusion, secure desktop, or operating-system policy.

### 2.4 Compatibility and maintainability

- **NFR-030 [MUST]** Production target is Windows 10 2004+ x64 or Windows 11 x64 (x64 only).
- **NFR-031 [MUST]** The solution shall compile with C++20 using a documented Visual Studio and WDK toolchain.
- **NFR-032 [MUST]** Warnings shall be treated as errors for first-party application code, with documented exceptions for imported sample code.
- **NFR-033 [MUST]** Application modules shall be built as static libraries and linked into one desktop executable.
- **NFR-034 [MUST]** Driver code shall remain a separate driver project and binary.
- **NFR-035 [MUST]** Public module interfaces shall be documented and avoid exposing raw ownership where practical.
- **NFR-036 [MUST]** The repository shall include deterministic build, package, test, and diagnostics scripts.

### 2.5 Usability and accessibility

- **NFR-040 [MUST]** The UI shall be Per-Monitor V2 DPI aware.
- **NFR-041 [MUST]** Primary functions shall be keyboard accessible.
- **NFR-042 [SHOULD]** Controls shall expose accessible names and states through standard Win32 accessibility mechanisms.
- **NFR-043 [MUST]** Errors shall include what failed, likely cause, and a recommended next action.
- **NFR-044 [MUST]** Destructive or irreversible actions shall not be triggered solely by closing the controller window.

## 3. Data requirements

### 3.1 Settings schema

```json
{
  "schemaVersion": 1,
  "outputDirectory": "C:\\Users\\User\\Videos\\Virtual Screen Recorder",
  "qualityPreset": "standard",
  "showPreview": true,
  "captureCursor": false,
  "minimizeToTray": true,
  "restoreTargetWindow": true
}
```

### 3.2 Runtime recording metadata

The runtime session shall track:

- Session correlation ID
- Target PID and HWND
- Target executable display name
- Virtual monitor identity and bounds
- Original and applied window placement
- Requested and actual encoding settings
- Start, pause, resume, and stop timestamps
- Captured, encoded, and dropped frame counts
- Temporary and final output paths
- Terminal result and diagnostic code

Runtime metadata is not required to be written beside the video in MVP 1.0.

## 4. Error code requirements

Minimum stable error codes:

- `VSR-INIT-OS-UNSUPPORTED`
- `VSR-INIT-DRIVER-MISSING`
- `VSR-INIT-D3D-FAILED`
- `VSR-DISPLAY-NOT-FOUND`
- `VSR-DISPLAY-DISCONNECTED`
- `VSR-TARGET-INVALID`
- `VSR-TARGET-LOST`
- `VSR-CAPTURE-INIT-FAILED`
- `VSR-CAPTURE-DEVICE-LOST`
- `VSR-ENCODER-UNAVAILABLE`
- `VSR-ENCODER-WRITE-FAILED`
- `VSR-OUTPUT-NOT-WRITABLE`
- `VSR-OUTPUT-DISK-LOW`
- `VSR-FINALIZE-FAILED`
- `VSR-INTERNAL-STATE`

Every user-visible terminal failure shall include one stable error code.

## 5. Acceptance criteria

### AC-001 Clean installation

**Given** a clean supported Windows 10 2004+ x64 or Windows 11 x64 machine  
**When** the signed installer is run with elevation  
**Then** the app and driver are installed, the app launches as standard user, and the virtual monitor is discoverable.

### AC-002 Virtual display isolation

**Given** the target application is on the virtual display and recording is active  
**When** unrelated windows are opened, moved, and switched on the physical display  
**Then** those windows do not appear in the resulting video.

### AC-003 Controller minimization

**Given** recording is active  
**When** the controller is minimized or hidden to the tray for at least five minutes  
**Then** recording continues and the output contains the full interval.

### AC-004 Window restoration

**Given** the target has a known original monitor, position, size, and state  
**When** recording stops normally  
**Then** the target is restored to the original placement within two seconds.

### AC-005 Pause and resume

**Given** recording runs for 10 seconds  
**When** it is paused for 10 seconds and resumed for another 10 seconds  
**Then** the final video's duration is approximately 20 seconds, not 30 seconds.

### AC-006 Target termination

**Given** recording is active  
**When** the target process exits  
**Then** the app stops safely, attempts finalization, returns to a non-recording state, and reports `VSR-TARGET-LOST`.

### AC-007 Disk failure

**Given** recording is active  
**When** free space reaches the critical threshold or a write fails  
**Then** the app stops accepting frames, attempts finalization, and reports the correct output error.

### AC-008 Long recording

**Given** Standard preset on a supported reference machine  
**When** recording runs continuously for two hours  
**Then** the app remains responsive, memory is bounded, timestamps remain monotonic, and the finalized file is playable.

### AC-009 Single instance

**Given** one controller instance is running  
**When** the executable is launched again  
**Then** the existing controller is activated and no second capture-capable process remains.

### AC-010 Uninstall

**Given** the application and driver are installed and no recording is active  
**When** uninstall is run with elevation  
**Then** application files and the driver package are removed without removing user recordings.

## 6. Traceability rule

Every implementation task shall reference one or more requirement IDs. Every MUST requirement shall be covered by at least one task and one automated or manual verification step before release.
