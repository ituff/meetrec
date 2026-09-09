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

    SavedPlacement saved;
    saved.hwnd = hwnd;
    saved.placement.length = sizeof(WINDOWPLACEMENT);
    if (!GetWindowPlacement(hwnd, &saved.placement)) {
        return ErrorFromWin32(GetLastError(), ErrorCode::TargetInvalid,
                              L"The selected window could not be inspected.",
                              L"GetWindowPlacement failed", true);
    }
    if (!GetWindowRect(hwnd, &saved.windowRect)) {
        return ErrorFromWin32(GetLastError(), ErrorCode::TargetInvalid,
                              L"The selected window could not be inspected.",
                              L"GetWindowRect failed", true);
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    saved.pid = pid;
    saved.sourceMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    GetMonitorRects(saved.sourceMonitor, &saved.sourceMonitorRect, nullptr);

    // Normal state must be sane before moving (maximized windows store the
    // normal rect in rcNormalPosition).
    const bool wasMaximized = saved.placement.showCmd == SW_SHOWMAXIMIZED;
    RECT normalRect = saved.placement.rcNormalPosition;

    if (wasMaximized) {
        ShowWindow(hwnd, SW_RESTORE);
    } else if (saved.placement.showCmd == SW_SHOWMINIMIZED) {
        ShowWindow(hwnd, SW_RESTORE);
        if (!GetWindowPlacement(hwnd, &saved.placement)) {
            return ErrorFromWin32(GetLastError(), ErrorCode::TargetInvalid,
                                  L"The selected window could not be inspected.",
                                  L"GetWindowPlacement failed", true);
        }
        normalRect = saved.placement.rcNormalPosition;
    }

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
               {"pid", std::to_string(pid)}});
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
