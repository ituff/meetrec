# ADR-003: Monitor capture via Windows.Graphics.Capture (WGC)

- **Status:** Accepted (vertical-slice validation T022 pending on reference machine)
- **Date:** 2026-09-09
- **Deciders:** Virtual Screen Recorder project
- **Related requirements:** FR-040..FR-049, FR-047, FR-048; design.md §4.6

## Context

Capture must target the virtual monitor (FR-040) with a GPU-backed D3D11 frame path (FR-041),
keep running while the controller window is hidden or minimized (FR-047), tolerate preview
teardown (FR-048), and work with the IddCx display stack. Candidate mechanisms:

1. **Windows.Graphics.Capture (`Direct3D11CaptureFramePool`, free-threaded)**
2. **DXGI Desktop Duplication** on the virtual output
3. **GDI `BitBlt`** from the monitor DC

## Decision

We will use **Windows.Graphics.Capture with a free-threaded `Direct3D11CaptureFramePool`**:

- The `GraphicsCaptureItem` is created from the virtual monitor's `HMONITOR` through
  `IGraphicsCaptureItemInterop::CreateForMonitor` (declared locally with the public contract
  GUID; the SDK ships no header for it).
- The pool runs free-threaded (`CreateFreeThreaded`), so frame callbacks arrive on a threadpool
  thread with no WinUI/Dispatcher dependency; the callback only copies the surface into a pooled
  BGRA texture and pushes it on the bounded queue (ADR-005).
- The frame pool is created on the same D3D11 device the encoder uses, keeping frames GPU-local.
- Cursor capture is toggled with `GraphicsCaptureSession.IsCursorCaptureEnabled` when present
  (FR-049, default off).

## Alternatives considered

- **DXGI Desktop Duplication** — rejected: duplicating a specific indirect output is fragile
  across driver updates (output index churn), requires per-frame timeout handling that couples
  well with neither the free-threaded queue nor mode changes, and duplicates the *entire
  desktop* of an output rather than integrating with the modern capture consent model.
- **GDI BitBlt** — rejected: CPU path (violates FR-041), no HDR-safe/DWM-composited path, and
  poor performance at 1080p60.

## Consequences

- (+) Frames arrive as D3D11 textures with no CPU copy; hidden/minimized controller windows
  cannot stop the flow because no HWND is involved (FR-047, AC-003).
- (+) The frame pool's own buffering plus our bounded queue gives predictable backpressure.
- (-) The WGC yellow border may render on some builds; suppressing it
  (`IsBorderRequired`) requires OS consent APIs and is out of MVP scope — documented in
  known limitations.
- (-) The interop activation interface must be declared in-tree (kept in
  `CaptureInterop.cpp` with its contract GUID).

## Validation evidence

- T022 vertical slice (`--dev-record`) exercises this exact path; 60-second minimized run and
  physical-desktop isolation checks are recorded per `docs/driver-manual-test-log.md` on the
  reference machine (pending driver installation evidence).
