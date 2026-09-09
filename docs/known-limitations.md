# Known limitations (MVP 1.0)

User-facing limitations follow spec.md §1 and §11. This file is the source for the README and
release notes sections with the same purpose.

## Rendering of the recorded application

- The recorded application must stay in a **normal rendering state on the virtual display**.
  Minimizing the target application may freeze or stop its rendering in the video — Windows may
  stop redrawing minimized applications (FR-030; the controller shows this note in the UI).
- Applications protected by DRM or that deliberately suppress capture will record as a black or
  frozen image. The product never attempts to bypass capture restrictions (NFR-025).
- Secure desktop surfaces (UAC consent, lock screen, sign-in) are never captured.

## Virtual display

- Exactly **one** virtual monitor ("Virtual Recording Display") is provided. Multiple virtual
  displays are out of MVP scope.
- The virtual display is SDR, 8-bit only, with modes 1280x720@30, 1920x1080@30 (preferred), and
  1920x1080@60. HDR, H.265/AV1, and 4K outputs are not supported.
- On builds that draw a yellow border around captured content, the border may be visible in the
  recording. Suppressing it requires OS consent APIs not used by the MVP.
- If the virtual display is disconnected mid-recording, the recorder stops **safely**,
  finalizes the file, and reports `VSR-DISPLAY-DISCONNECTED` — the video up to that point is
  preserved (FR-015).

## Driver signing (internal builds)

- Internal builds ship a **test-signed** driver. Installing it requires Windows Test Mode
  (`bcdedit /set testsigning on`) and trusting the exported certificate. Production-signed
  distribution is a separate release gate (spec.md §13).
- Third-party display mirroring software, GPU driver rollbacks mid-session, and virtualization
  (Remote Desktop, Sandbox, nested GPU) can break virtual display creation or capture and are
  outside the support boundary (spec.md §11).

## Recording behavior

- **No audio** in MVP 1.0 (FR-061).
- Recordings pause correctly (paused time is excluded from the output, AC-005), but frames are
  not buffered during pause: what happens on the virtual display during a pause is not recorded
  and cannot be recovered.
- If free disk space hits the documented stop threshold — max(256 MB, 30 seconds at the active
  bitrate) — recording stops and finalizes automatically (`VSR-OUTPUT-DISK-LOW`, AC-007).
- A hard crash or power loss can leave an unfinalized `.partial.mp4`; the file may not be
  playable. Controlled stops always finalize (design.md risk R-007).
- If no hardware H.264 encoder is available, the recorder falls back to the Microsoft software
  encoder (higher CPU use); with no software encoder either, it fails before recording with
  `VSR-ENCODER-UNAVAILABLE` (FR-063).

## Environment

- Windows 11 x64 only; interactive sessions only. Remote Desktop, Windows Sandbox, and
  unattended/services contexts are unsupported (NFR-030, spec.md §11).
- Mixed-DPI environments are supported (Per-Monitor V2), but applications that misbehave when
  moved between DPI boundaries may render oddly after placement — the recorder restores their
  original placement on stop.
