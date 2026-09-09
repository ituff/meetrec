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

    // Restores the original placement, or a visible primary-monitor fallback
    // (FR-026, FR-027, NFR-013). Best effort: returns an error only when even
    // the fallback could not place the window visibly.
    Result<void> Restore(const SavedPlacement& saved);

private:
    static bool MonitorStillExists(HMONITOR monitor);
};

} // namespace vsr::window
