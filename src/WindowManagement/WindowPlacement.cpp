#include "WindowPlacement.h"

#include <algorithm>

#include <Infrastructure/Log.h>
#include <Infrastructure/Strings.h>

namespace vsr::window {

RECT ComputeDestinationRect(const RECT& windowRect, const RECT& workArea) {
    const long winW = windowRect.right - windowRect.left;
    const long winH = windowRect.bottom - windowRect.top;
    const long areaW = workArea.right - workArea.left;
    const long areaH = workArea.bottom - workArea.top;

    long w = winW;
    long h = winH;
    if (areaW > 0 && areaH > 0) {
        // Clamp into the work area (FR-024: wholly inside the work area).
        if (w > areaW) w = areaW;
        if (h > areaH) h = areaH;
        if (w <= 0) w = areaW / 2;
        if (h <= 0) h = areaH / 2;
    }

    long x = workArea.left + (areaW > w ? (areaW - w) / 2 : 0);
    long y = workArea.top + (areaH > h ? (areaH - h) / 2 : 0);
    // Keep the title bar reachable even when the work area is smaller than
    // the window (degenerate case): pin to the top-left of the work area.
    if (areaW <= w) x = workArea.left;
    if (areaH <= h) y = workArea.top;

    RECT dest = windowRect;
    dest.left = x;
    dest.top = y;
    dest.right = x + w;
    dest.bottom = y + h;
    return dest;
}

RECT ComputeOffScreenRect(const RECT& windowRect, const RECT& virtualDesktop) {
    const long winW = windowRect.right - windowRect.left;
    const long winH = windowRect.bottom - windowRect.top;
    constexpr long kParkMargin = 64;

    // Park left of the whole virtual desktop; the union bounds cover every
    // monitor, so the rect is guaranteed invisible. The top coordinate only
    // matters for tidiness — a fully off-screen x hides the window regardless.
    const long x = virtualDesktop.left - winW - kParkMargin;

    RECT out = windowRect;
    out.left = x;
    out.right = x + winW;
    out.top = virtualDesktop.top;
    out.bottom = virtualDesktop.top + winH;
    return out;
}

RECT ComputeOnScreenCornerRect(const RECT& windowRect, const RECT& workArea) {
    const long winW = windowRect.right - windowRect.left;
    const long winH = windowRect.bottom - windowRect.top;

    // Clamp into the work area, then tuck into the bottom-right corner.
    long w = winW;
    long h = winH;
    const long areaW = workArea.right - workArea.left;
    const long areaH = workArea.bottom - workArea.top;
    if (areaW > 0 && w > areaW) w = areaW;
    if (areaH > 0 && h > areaH) h = areaH;

    long x = workArea.right - 8 - w;
    long y = workArea.bottom - 8 - h;

    RECT out = windowRect;
    out.left = x;
    out.top = y;
    out.right = x + w;
    out.bottom = y + h;
    return out;
}

namespace {

void GetMonitorRects(HMONITOR monitor, RECT* monitorRect, RECT* workRect) {
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (monitor != nullptr && GetMonitorInfoW(monitor, &info)) {
        if (monitorRect != nullptr) *monitorRect = info.rcMonitor;
        if (workRect != nullptr) *workRect = info.rcWork;
    }
}

// Restores a maximized window onto a given monitor: place a normal rect into
// the target monitor first, then re-maximize (FR-025).
void MaximizeOnMonitor(HWND hwnd, const RECT& monitorRect) {
    SetWindowPos(hwnd, nullptr, monitorRect.left + 8, monitorRect.top + 8,
                 std::max(200L, (monitorRect.right - monitorRect.left) / 2),
                 std::max(150L, (monitorRect.bottom - monitorRect.top) / 2),
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(hwnd, &placement)) {
        placement.showCmd = SW_SHOWMAXIMIZED;
        SetWindowPlacement(hwnd, &placement);
    }
}

// Shared validation + placement capture for both move targets (virtual
// display, off-screen park). Restores a maximized/minimized window to normal
// state so SetWindowPos can position it, and reports the pre-move state.
struct PlacementSnapshot {
    SavedPlacement saved;
    bool wasMaximized = false;
    RECT normalRect = {};
};

Result<PlacementSnapshot> CapturePlacementForMove(HWND hwnd) {
    PlacementSnapshot snap;
    snap.saved.hwnd = hwnd;
    snap.saved.placement.length = sizeof(WINDOWPLACEMENT);
    if (!GetWindowPlacement(hwnd, &snap.saved.placement)) {
        return ErrorFromWin32(GetLastError(), ErrorCode::TargetInvalid,
                              L"The selected window could not be inspected.",
                              L"GetWindowPlacement failed", true);
    }
    if (!GetWindowRect(hwnd, &snap.saved.windowRect)) {
        return ErrorFromWin32(GetLastError(), ErrorCode::TargetInvalid,
                              L"The selected window could not be inspected.",
                              L"GetWindowRect failed", true);
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    snap.saved.pid = pid;
    snap.saved.sourceMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    GetMonitorRects(snap.saved.sourceMonitor, &snap.saved.sourceMonitorRect, nullptr);

    // Normal state must be sane before moving (maximized windows store the
    // normal rect in rcNormalPosition).
    snap.wasMaximized = snap.saved.placement.showCmd == SW_SHOWMAXIMIZED;
    snap.normalRect = snap.saved.placement.rcNormalPosition;

    if (snap.wasMaximized) {
        ShowWindow(hwnd, SW_RESTORE);
    } else if (snap.saved.placement.showCmd == SW_SHOWMINIMIZED) {
        ShowWindow(hwnd, SW_RESTORE);
        if (!GetWindowPlacement(hwnd, &snap.saved.placement)) {
            return ErrorFromWin32(GetLastError(), ErrorCode::TargetInvalid,
                                  L"The selected window could not be inspected.",
                                  L"GetWindowPlacement failed", true);
        }
        snap.normalRect = snap.saved.placement.rcNormalPosition;
    }
    return snap;
}

} // namespace

bool WindowPlacementManager::MonitorStillExists(HMONITOR monitor) {
    if (monitor == nullptr) return false;
    // A saved HMONITOR is only valid if some monitor currently reports the
    // same handle; walk the current monitor list to check.
    struct Ctx {
        HMONITOR target;
        bool found;
    } ctx{monitor, false};
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR h, HDC, LPRECT, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            if (h == c->target) c->found = true;
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

Result<SavedPlacement> WindowPlacementManager::CaptureAndMoveTo(HWND hwnd,
                                                                const RECT& virtualWorkArea,
                                                                HMONITOR virtualMonitor) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return Error(ErrorCode::TargetInvalid,
                     L"The selected window is no longer available. Pick a window that is "
                     L"still open.",
                     L"CaptureAndMoveTo called with invalid hwnd", E_POINTER, true);
    }
    // Cross-process windows must accept being moved; check hung state.
    if (IsHungAppWindow(hwnd)) {
        return Error(ErrorCode::TargetInvalid,
                     L"The selected application is not responding. Close or restart it, then "
                     L"pick it again.",
                     L"IsHungAppWindow reported the target is hung", E_FAIL, true);
    }

    auto snapResult = CapturePlacementForMove(hwnd);
    if (!snapResult) return snapResult.TakeError();
    PlacementSnapshot snap = snapResult.Value();
    SavedPlacement& saved = snap.saved;
    const bool wasMaximized = snap.wasMaximized;
    RECT normalRect = snap.normalRect;

    RECT dest = ComputeDestinationRect(normalRect, virtualWorkArea);
    SetWindowPos(hwnd, nullptr, dest.left, dest.top, dest.right - dest.left,
                 dest.bottom - dest.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    const HMONITOR resultingMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (resultingMonitor != virtualMonitor) {
        // Retry once with an explicit interior rectangle.
        dest = ComputeDestinationRect(
            RECT{virtualWorkArea.left + 8, virtualWorkArea.top + 8, virtualWorkArea.left + 800,
                 virtualWorkArea.top + 600},
            virtualWorkArea);
        SetWindowPos(hwnd, nullptr, dest.left, dest.top, dest.right - dest.left,
                     dest.bottom - dest.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        if (MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) != virtualMonitor) {
            // Move failed; restore immediately so the target is never lost.
            (void)Restore(saved);
            return Error(ErrorCode::TargetInvalid,
                         L"Windows refused to move the selected application to the virtual "
                         L"display. Try a different application window.",
                         L"Target window did not land on the virtual monitor", S_OK, true);
        }
    }

    if (wasMaximized) {
        MaximizeOnMonitor(hwnd, virtualWorkArea);
    }

    log::Info("WindowManagement", "TARGET_MOVED", L"",
              L"Target window moved to the virtual display.",
              {{"wasMaximized", wasMaximized ? "1" : "0"},
               {"pid", std::to_string(saved.pid)}});
    return saved;
}

Result<SavedPlacement> WindowPlacementManager::CaptureAndParkOffScreen(HWND hwnd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return Error(ErrorCode::TargetInvalid,
                     L"The selected window is no longer available. Pick a window that is "
                     L"still open.",
                     L"CaptureAndParkOffScreen called with invalid hwnd", E_POINTER, true);
    }
    if (IsHungAppWindow(hwnd)) {
        return Error(ErrorCode::TargetInvalid,
                     L"The selected application is not responding. Close or restart it, then "
                     L"pick it again.",
                     L"IsHungAppWindow reported the target is hung", E_FAIL, true);
    }

    auto snapResult = CapturePlacementForMove(hwnd);
    if (!snapResult) return snapResult.TakeError();
    SavedPlacement saved = snapResult.Value().saved;

    // Park the target. Local sessions park fully off-screen (invisible). In a
    // remote (RDP) session DWM may not compose fully off-screen windows, which
    // makes them uncapturable, so the target stays on-screen but is sent to
    // the bottom of the Z-order — occluded, hence invisible in practice, and
    // still composed for capture.
    const bool remoteSession = GetSystemMetrics(SM_REMOTESESSION) != 0;
    RECT park{};
    HWND insertAfter = nullptr;
    if (remoteSession) {
        const HMONITOR primary = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        RECT work{};
        GetMonitorRects(primary, nullptr, &work);
        park = ComputeOnScreenCornerRect(snapResult.Value().normalRect, work);
        insertAfter = HWND_BOTTOM;
    } else {
        RECT virtualDesktop{};
        virtualDesktop.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        virtualDesktop.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        virtualDesktop.right = virtualDesktop.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        virtualDesktop.bottom = virtualDesktop.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (virtualDesktop.right <= virtualDesktop.left) {
            // Degenerate metrics: fall back to parking left of the origin.
            virtualDesktop.right = virtualDesktop.left;
        }
        park = ComputeOffScreenRect(snapResult.Value().normalRect, virtualDesktop);
    }
    SetWindowPos(hwnd, insertAfter, park.left, park.top, park.right - park.left,
                 park.bottom - park.top, SWP_NOACTIVATE | SWP_FRAMECHANGED);

    log::Info("WindowManagement", "TARGET_PARKED", L"",
              remoteSession ? L"Target window parked on-screen (remote session)."
                            : L"Target window parked off-screen for driverless recording.",
              {{"wasMaximized", snapResult.Value().wasMaximized ? "1" : "0"},
               {"remoteSession", remoteSession ? "1" : "0"},
               {"pid", std::to_string(saved.pid)}});
    return saved;
}

Result<void> WindowPlacementManager::Restore(const SavedPlacement& saved) {
    if (saved.hwnd == nullptr || !IsWindow(saved.hwnd)) {
        // Window already destroyed; nothing to restore (FR-028 path).
        return Result<void>{};
    }

    const bool sourceAlive = MonitorStillExists(saved.sourceMonitor);
    const bool wasMaximized = saved.placement.showCmd == SW_SHOWMAXIMIZED;

    // Primary-monitor fallback geometry, prepared even when unused.
    const HMONITOR primary = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    RECT work{};
    GetMonitorRects(primary, nullptr, &work);

    if (sourceAlive) {
        if (wasMaximized) {
            MaximizeOnMonitor(saved.hwnd, saved.sourceMonitorRect);
        } else {
            const BOOL placed = SetWindowPlacement(saved.hwnd, &saved.placement);
            if (!placed) {
                log::Warning("WindowManagement", "TARGET_RESTORE_PLACEMENT_FAILED", L"",
                             L"SetWindowPlacement failed; falling back to SetWindowPos.");
                const RECT dest = ComputeDestinationRect(saved.placement.rcNormalPosition, work);
                SetWindowPos(saved.hwnd, nullptr, dest.left, dest.top, dest.right - dest.left,
                             dest.bottom - dest.top,
                             SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
                ShowWindow(saved.hwnd, SW_SHOW);
            }
        }
    } else {
        // FR-027 / NFR-013: never leave the window off-screen; fall back to
        // the primary monitor work area, visibly.
        const RECT dest = ComputeDestinationRect(saved.placement.rcNormalPosition, work);
        SetWindowPos(saved.hwnd, nullptr, dest.left, dest.top, dest.right - dest.left,
                     dest.bottom - dest.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        ShowWindow(saved.hwnd, SW_SHOW);
        log::Warning("WindowManagement", "TARGET_RESTORED_FALLBACK", L"",
                     L"Source monitor unavailable; target restored on the primary monitor.");
    }

    log::Info("WindowManagement", "TARGET_RESTORED", L"", L"Target window restored.");
    return Result<void>{};
}

} // namespace vsr::window
