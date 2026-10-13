// Virtual Screen Recorder - target placement and restoration (FR-023..FR-029,
// NFR-013, NFR-040; T051).
//
// Placement sequence (design.md §4.5):
//   validate -> capture WINDOWPLACEMENT and source monitor -> restore if
//   maximized -> compute destination rect -> SetWindowPos -> re-maximize ->
//   verify resulting monitor.
//
// Restoration is best effort but must guarantee visibility (NFR-013): if the
// original monitor is gone, the window is restored onto the primary monitor
// work area.
#pragma once

#include <windows.h>

#include <Infrastructure/Error.h>
#include <Infrastructure/Result.h>

namespace vsr::window {

// Pure geometry: fits `windowRect` inside `workArea`, centered, clamped to
// the work area size, keeping the top-left at least at the work-area origin.
// Unit-testable without a display stack.
RECT ComputeDestinationRect(const RECT& windowRect, const RECT& workArea);

// Pure geometry: places `windowRect` entirely outside the union of all
// monitor rects (`virtualDesktop`, i.e. SM_XVIRTUALSCREEN..SM_XVIRTUALSCREEN +
// SM_CXVIRTUALSCREEN) on its left side, with a safety margin. Driverless
// window recording parks the target here so it stays composed (capturable)
// while invisible. Unit-testable without a display stack.
RECT ComputeOffScreenRect(const RECT& windowRect, const RECT& virtualDesktop);

// Pure geometry: tucks `windowRect` into the bottom-right corner of `workArea`
// (size preserved, wholly inside). Used for remote (RDP) sessions, where DWM
// may not compose fully off-screen windows: the target stays on-screen but is
// sent to the bottom of the Z-order so everything else occludes it. The
// capture reads the window content regardless of occlusion. Unit-testable.
RECT ComputeOnScreenCornerRect(const RECT& windowRect, const RECT& workArea);

struct SavedPlacement {
    HWND hwnd = nullptr;
    DWORD pid = 0;
    WINDOWPLACEMENT placement = {};
    RECT sourceMonitorRect = {};   // desktop rect of the monitor it came from
    HMONITOR sourceMonitor = nullptr;
    RECT windowRect = {};          // rect when captured
};

class WindowPlacementManager {
public:
    // Captures the original placement and moves the window into the virtual
    // monitor work area (FR-023..FR-025).
    Result<SavedPlacement> CaptureAndMoveTo(HWND hwnd, const RECT& virtualWorkArea,
                                            HMONITOR virtualMonitor);

    // Captures the original placement and parks the window entirely off-screen
    // (driverless window recording): invisible on every monitor, still
    // composed, so the window capture keeps receiving frames. Restoration uses
    // the same SavedPlacement/Restore path as CaptureAndMoveTo.
    Result<SavedPlacement> CaptureAndParkOffScreen(HWND hwnd);

    // Restores the original placement, or a visible primary-monitor fallback
    // (FR-026, FR-027, NFR-013). Best effort: returns an error only when even
    // the fallback could not place the window visibly.
    Result<void> Restore(const SavedPlacement& saved);

private:
    static bool MonitorStillExists(HMONITOR monitor);
};

} // namespace vsr::window
