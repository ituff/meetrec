# Virtual Screen Recorder MVP Technical Design

- **Document ID:** VSR-MVP-DES-001
- **Version:** 1.0
- **Status:** Implementation baseline

## 1. Architecture decision

Use a native Windows architecture composed of:

1. One Win32 desktop executable linked from static application libraries.
2. One UMDF 2 indirect display driver based on IddCx.
3. Windows.Graphics.Capture for monitor capture.
4. Direct3D 11 for GPU frame transport and conversion.
5. Media Foundation for H.264 MP4 encoding.
6. Inno Setup for the internal and distributable installer workflow.

The MVP does not use Electron, a browser runtime, .NET Desktop Runtime, FFmpeg, a Windows service, or a separate background agent.

## 2. System context

```text
User
  |
  v
Win32 Controller + Tray
  |
  +--> Application Coordinator / State Machine
          |
          +--> Virtual Display Controller --> UMDF 2 / IddCx Driver --> Windows Display Stack
          |
          +--> Window Placement Manager --> Win32 Window Manager
          |
          +--> Capture Session --> Windows.Graphics.Capture --> D3D11 Textures
          |                                                |
          |                                                v
          +--> Encoder Pipeline <-------------------- Bounded Frame Queue
          |         |
          |         +--> Media Foundation H.264 MFT --> MP4 Sink Writer --> .partial.mp4
          |
          +--> Settings Store / Structured Log / Diagnostics
```

## 3. Repository layout

```text
virtual-screen-recorder/
├─ VirtualScreenRecorder.sln
├─ Directory.Build.props
├─ README.md
├─ specs/001-mvp/
│  ├─ spec.md
│  ├─ requirements.md
│  ├─ design.md
│  └─ tasks.md
├─ src/
│  ├─ App/
│  ├─ Application/
│  ├─ Capture/
│  ├─ Encoder/
│  ├─ DisplayControl/
│  ├─ WindowManagement/
│  ├─ Infrastructure/
│  └─ Driver/
├─ tests/
│  ├─ UnitTests/
│  ├─ IntegrationTests/
│  └─ ManualTests/
├─ installer/
├─ scripts/
└─ artifacts/
```

All first-party application libraries are statically linked into `VirtualScreenRecorder.exe`. The UMDF driver remains separately packaged as required by Windows.

## 4. Component design

### 4.1 App shell

Responsibilities:

- Win32 process startup and shutdown
- COM and WinRT initialization
- Per-Monitor V2 DPI awareness
- Single-instance mutex and activation message
- Main window and message loop
- Taskbar notification icon
- Dispatch UI commands to the application coordinator

The UI must not directly operate capture, encoder, display, or driver objects.

### 4.2 Application coordinator

The coordinator is the authoritative owner of product state. It validates commands, orders subsystem startup and shutdown, and publishes immutable status snapshots to the UI.

Suggested interface:

```cpp
enum class AppState {
    Initializing,
    Ready,
    Preparing,
    Recording,
    Paused,
    Stopping,
    Faulted,
    ShuttingDown
};

struct StartRecordingRequest {
    HWND targetWindow;
    std::filesystem::path outputDirectory;
    QualityPreset preset;
    bool captureCursor;
};

class IApplicationCoordinator {
public:
    virtual Result<void> Start(const StartRecordingRequest&) = 0;
    virtual Result<void> Pause() = 0;
    virtual Result<void> Resume() = 0;
    virtual Result<void> Stop(StopReason) = 0;
    virtual StatusSnapshot GetStatus() const = 0;
    virtual ~IApplicationCoordinator() = default;
};
```

Command handling must be serialized. Long operations run asynchronously and report completion back to the coordinator.

### 4.3 Virtual display driver

The driver is derived from the Microsoft IDD sample structure but must be reduced to one adapter and one monitor, with sample-only assumptions and TODOs reviewed.

Responsibilities:

- Initialize UMDF and IddCx adapter
- Report one monitor arrival
- Report supported modes and preferred mode
- Accept and release swap chains correctly
- Handle power and device lifecycle callbacks
- Emit WPP trace events

The driver shall not encode video, write files, call windowing APIs, or host application UI.

#### Driver identity

Use stable values assigned once and stored in source-controlled configuration:

- Provider name
- Device description
- Hardware ID
- Device interface GUID
- Monitor connector index
- Driver version

The application shall locate the virtual monitor using device-interface or display-path identity. It shall not assume `DISPLAY2` or any fixed monitor ordinal.

#### Driver control

For the MVP, prefer a software-device lifecycle managed by the installed application or installation workflow. If runtime creation through `SwDeviceCreate` proves unreliable for the selected driver packaging, keep the installed device present and expose application-level enable or disable through supported device APIs. This decision must be finalized during the driver spike and recorded as an ADR.

### 4.4 Display topology service

Responsibilities:

- Enumerate active display paths and monitors
- Map stable virtual device identity to `HMONITOR` and desktop bounds
- Validate extended-desktop participation
- Obtain monitor work area and DPI
- Detect topology changes

Suggested immutable model:

```cpp
struct MonitorDescriptor {
    std::wstring stableId;
    HMONITOR handle;
    RECT monitorRect;
    RECT workRect;
    UINT dpiX;
    UINT dpiY;
    bool primary;
    bool virtualRecordingDisplay;
};
```

Topology-change notifications shall be debounced before reevaluation.

### 4.5 Window discovery and placement

Responsibilities:

- Enumerate eligible windows
- Resolve process executable display name and icon
- Capture original placement
- Move target to virtual display
- Restore target safely
- Detect target destruction or invalidation
- Monitor relevant new windows from the same process

Eligibility baseline:

- `IsWindowVisible(hwnd)` is true
- Root top-level window
- Non-empty title
- Not owned by recorder process
- Not cloaked according to DWM attributes
- Not a tool window unless explicitly allowed later

Placement sequence:

```text
Validate HWND and process
-> Capture WINDOWPLACEMENT and source monitor
-> If maximized, ShowWindow(SW_RESTORE)
-> Calculate destination rectangle within virtual work area
-> SetWindowPos with DPI-aware dimensions
-> Restore maximized state when originally maximized
-> Verify resulting monitor
```

Restoration is best effort and must guarantee visibility. If the source monitor disappeared, use the primary monitor work area.

### 4.6 Capture session

Responsibilities:

- Create a `GraphicsCaptureItem` for the virtual monitor
- Create D3D11 device with BGRA support
- Create and manage `Direct3D11CaptureFramePool`
- Receive frames and extract `ID3D11Texture2D`
- Detect content-size changes
- Attach monotonic capture timestamps
- Submit frame references to the bounded queue
- Track captured and dropped counts

Threading rules:

- Frame callback performs minimal work.
- It must not block on disk I/O or UI operations.
- The queue capacity is fixed, initially three frames.
- When full, evict the oldest unencoded frame and increment dropped count.
- The capture session owns no HWND that must remain visible.

Frame ownership must prevent reuse of a capture-pool surface after the callback. Either copy into a small reusable D3D11 texture pool or retain the frame object only for the documented safe lifetime. The implementation spike must prove the selected method.

### 4.7 D3D11 processing

The encoder commonly requires NV12 while capture commonly supplies BGRA8. The pipeline shall inspect the activated encoder requirements and use one of these paths:

1. Direct texture submission when the MFT accepts the capture format.
2. D3D11 video processor conversion from BGRA to NV12.
3. A documented GPU shader conversion only if the video processor path is unavailable.

CPU readback is prohibited in the normal recording path.

Manage device loss centrally. A D3D device-loss signal stops capture and attempts orderly finalization rather than silently recreating the pipeline during MVP recording.

### 4.8 Encoder pipeline

Responsibilities:

- Initialize Media Foundation once per process
- Select an H.264 encoder MFT
- Prefer D3D11-aware hardware transforms
- Configure width, height, frame rate, bitrate, and progressive scan
- Create MP4 sink writer or equivalent Media Foundation sink
- Submit samples with monotonic time and duration
- Drain and finalize on Stop
- Rename `.partial.mp4` after success

#### Time model

Use a monotonic clock, not wall-clock time, for sample timestamps.

```text
output_timestamp = current_monotonic_time
                 - recording_start_time
                 - accumulated_paused_duration
```

Clamp or reject timestamps that would not be strictly greater than the previous submitted sample. Frame duration is derived from the selected nominal frame rate.

#### Pause model

Pause keeps capture alive but discards or ignores frames before encoder submission. Resume resets the capture-to-output timing anchor and excludes paused duration. The queue is cleared when entering Paused and before Resume.

#### Finalization model

`Stop()` is idempotent and follows:

```text
Stop accepting frames
-> unsubscribe capture callback
-> close capture session
-> signal encoder queue completion
-> encode remaining accepted frames
-> drain encoder
-> finalize sink
-> close file
-> rename partial file
-> restore target window
-> publish terminal result
```

If finalization fails, retain the `.partial.mp4` file and expose its path in diagnostics.

### 4.9 Preview renderer

The preview consumes sampled capture frames but is not the capture source. It may render at a lower update rate, such as 10 FPS. Closing or minimizing preview releases preview-specific swap-chain resources but does not alter the capture or encoder sessions.

Preview is optional for initial vertical-slice completion and becomes required before P0 completion.

### 4.10 Settings

Use a small UTF-8 JSON file stored under LocalAppData. Use atomic replacement:

```text
write settings.json.tmp
-> flush
-> replace settings.json
```

Settings access is serialized. Invalid fields use defaults. Avoid a large external JSON dependency if Windows-native or a compact audited implementation is adequate; if a library is selected, record license and binary-size impact.

### 4.11 Logging and diagnostics

Provide a lightweight structured logger with asynchronous or low-contention writes. Every recording session gets a correlation ID.

Example log shape:

```json
{
  "timestampUtc": "2026-09-09T12:40:00.123Z",
  "level": "Info",
  "component": "Encoder",
  "eventId": "ENCODER_FINALIZED",
  "correlationId": "...",
  "message": "Output finalized",
  "properties": {
    "durationMs": 1800000,
    "encodedFrames": 53998,
    "droppedFrames": 2
  }
}
```

Do not log window content. Window titles should be omitted or sanitized in diagnostics packages because they may contain document names.

### 4.12 Installer

Internal MVP installer responsibilities:

- Require x64 Windows 11
- Install application under Program Files
- Install driver package with elevated system tooling
- Create Start Menu shortcut
- Launch application optionally after install
- Stop the application before upgrade or uninstall
- Remove driver package during uninstall
- Preserve recordings and user settings by default

The installer must surface driver-install return codes and write an installation log. Test-signing scripts belong under `scripts/` and are not shipped in the distributable package.

## 5. Concurrency model

Recommended threads:

- UI thread: Win32 message pump and UI state application
- Capture callback thread or dispatcher: minimal frame acquisition
- Encoder worker: queue consumption and Media Foundation submission
- Logging worker: optional buffered file output

Rules:

- No synchronous waits on encoder completion from a capture callback.
- No UI API from background threads. Post messages or use a dispatcher.
- Coordinator transitions are guarded by one serialization mechanism.
- Shutdown uses cancellation tokens or events and bounded waits.
- Lock ordering is documented; avoid holding locks while invoking external callbacks.

## 6. Error-handling design

Use a typed result model for expected errors and exceptions for unexpected local construction failures only.

```cpp
struct Error {
    ErrorCode code;
    HRESULT hresult;
    std::wstring userMessage;
    std::wstring technicalMessage;
    bool recoverable;
};

template<class T>
using Result = /* expected-like implementation */;
```

At module boundaries:

- Convert Win32 error to `HRESULT_FROM_WIN32`.
- Preserve originating HRESULT.
- Map to one stable VSR error code.
- Log technical context.
- Present a concise message and recommended action.

## 7. Build design

### 7.1 Toolchain

- Visual Studio 2022 with Desktop development with C++
- Windows 11 SDK
- Matching Windows Driver Kit
- C++20
- MSBuild solution for application and driver

Pin and document exact tested versions in `README.md` and CI configuration.

### 7.2 Configurations

- `Debug|x64`: developer diagnostics, unsigned app, test driver workflow
- `Release|x64`: optimized internal build
- `ReleaseSigned|x64`: production signing hooks, no keys in repository

### 7.3 Build outputs

```text
artifacts/<configuration>/
├─ app/VirtualScreenRecorder.exe
├─ driver/VirtualDisplayDriver.dll
├─ driver/VirtualDisplayDriver.inf
├─ driver/VirtualDisplayDriver.cat
├─ symbols/
└─ installer/VirtualScreenRecorderSetup.exe
```

## 8. Testing design

### 8.1 Unit tests

Prioritize platform-independent or abstracted logic:

- State transitions
- Preset validation
- Timestamp calculations
- Pause duration calculation
- Bounded queue policy
- File name and path validation
- Settings parsing and migration
- Error mapping
- Window destination-rectangle calculation

### 8.2 Integration tests

Run on a Windows 11 machine or VM capable of loading the test-signed driver:

- Driver install and removal
- Monitor discovery
- Capture item creation
- Five-second encoded sample
- Stop and finalization
- Target window move and restore
- Device or target loss handling where automatable

### 8.3 Manual tests

- Physical desktop isolation
- Tray behavior
- Multi-DPI placement
- Different GPU vendors
- Sleep, lock, and display topology changes
- Long recording
- Installer upgrade and uninstall

### 8.4 Media validation

Validation must inspect:

- Container opens successfully
- H.264 video stream exists
- Expected dimensions and nominal frame rate
- Duration tolerance
- Monotonic timestamps
- Decoding succeeds from beginning to end

Use a test-only inspection tool if needed. Do not ship that dependency with the product.

## 9. Key risks and mitigations

### R-001 Driver signing and distribution

**Risk:** Internal test-signed builds work, but external installation fails under normal Windows signing policy.  
**Mitigation:** Treat distributable signing as a separate release gate; validate installation on a clean Secure Boot machine early.

### R-002 IDD sample production gaps

**Risk:** Sample code contains assumptions or TODOs unsuitable for production.  
**Mitigation:** Maintain a sample-review checklist, remove unused monitor paths, add lifecycle tests, and document every retained sample block.

### R-003 Encoder interoperability

**Risk:** Hardware MFT capabilities differ across GPU vendors.  
**Mitigation:** Build an encoder capability probe, test at least two vendors, and fail before recording if no supported path exists.

### R-004 GPU format conversion

**Risk:** BGRA capture cannot be submitted directly to the selected encoder.  
**Mitigation:** Implement and measure D3D11 video-processor conversion to NV12 during the vertical slice.

### R-005 Applications stop rendering

**Risk:** Some applications throttle when off the physical screen, occluded, or minimized.  
**Mitigation:** Keep the target non-minimized, document limitations, and test representative Win32, Chromium, and Office-style applications.

### R-006 Display topology changes

**Risk:** Windows repositions windows or disconnects the virtual monitor.  
**Mitigation:** Subscribe to topology changes and stop safely if the stable monitor identity disappears.

### R-007 MP4 corruption on abnormal termination

**Risk:** A hard crash or power loss leaves an unfinalized file.  
**Mitigation:** Use `.partial.mp4`, finalize on controlled paths, and document that hard termination recovery is not guaranteed in MVP.

## 10. Architectural decision records required

Before P0 completion, create:

- `ADR-001-native-cpp-win32.md`
- `ADR-002-idd-driver-lifecycle.md`
- `ADR-003-monitor-capture-api.md`
- `ADR-004-media-foundation-encoding.md`
- `ADR-005-frame-ownership-and-queue.md`
- `ADR-006-installer-and-driver-signing.md`

Each ADR includes context, decision, alternatives, consequences, and validation evidence.

## 11. Codex implementation rules

1. Implement only the task currently assigned and its explicit prerequisites.
2. Do not broaden MVP scope without updating the four spec documents.
3. Reference requirement IDs in pull request or commit descriptions.
4. Prefer small commits with compiling intermediate states.
5. Do not replace native components with Electron, .NET, FFmpeg, OBS libraries, or third-party virtual-display binaries without an ADR and spec change.
6. Do not copy the Microsoft sample blindly. Mark imported code and review every TODO.
7. Never commit certificates, private keys, secrets, generated driver packages, recordings, or user logs.
8. Add tests alongside logic. A task is incomplete if its stated verification is absent.
9. Preserve the last known compiling state. Feature flags or stubs are acceptable during vertical slicing.
10. When an API assumption is uncertain, create a minimal spike and record observed results before designing around it.
