#include "PopupTracker.h"

#include <dwmapi.h>

#include <algorithm>
#include <thread>

#include <Infrastructure/Log.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")

namespace vsr::window {

bool ShouldRelocatePopup(const PopupCandidate& c) noexcept {
    if (!c.visible) return false;
    if (c.mainFrame) return false;            // already placed by the coordinator
    if (!c.topLevel) return false;            // child/popups of controls only
    if (!c.sameProcess) return false;         // never touch other processes
    if (!c.hasCaption) return false;          // borderless helper surfaces stay put
    if (c.isToolWindow) return false;
    if (c.cloaked) return false;              // UWP suspended frames etc.
    if (c.alreadyOnVirtual) return false;     // nothing to do
    // A caption without any title and without ownership is usually a message
    // or splash surface; require some link to the recorded window.
    return !c.emptyTitle || c.ownedByTarget;
}

PopupTracker* PopupTracker::active_ = nullptr;

namespace {

bool IsCloaked(HWND hwnd) {
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return cloaked != FALSE;
}

} // namespace

PopupTracker::~PopupTracker() {
    Stop();
}

Result<void> PopupTracker::Start(HWND targetHwnd, const RECT& virtualWorkArea) {
    if (targetHwnd == nullptr || !IsWindow(targetHwnd)) {
        return Error(ErrorCode::TargetInvalid, L"The recorded window is no longer available.",
                     L"PopupTracker::Start invalid target", E_POINTER, true);
    }
    Stop(); // defensive: never run two hooks

    target_ = targetHwnd;
    virtualWorkArea_ = virtualWorkArea;
    GetWindowThreadProcessId(targetHwnd, &targetPid_);

    // The hook delivers events through the message loop of the thread that
    // installed it, so installation happens on the dedicated pump thread.
    thread_ = CreateThread(nullptr, 0,
                           [](LPVOID param) -> DWORD {
                               auto* self = static_cast<PopupTracker*>(param);
                               active_ = self;
                               self->hook_ = SetWinEventHook(
                                   EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr,
                                   &PopupTracker::WinEventProc, self->targetPid_, 0,
                                   WINEVENT_OUTOFCONTEXT);
                               if (self->hook_ == nullptr) {
                                   log::Warning("WindowManagement", "POPUP_HOOK_FAILED", L"",
                                                L"SetWinEventHook failed; popups will not be "
                                                L"relocated (FR-029 best effort).");
                               }
                               MSG msg;
                               BOOL more = TRUE;
                               while (more && GetMessageW(&msg, nullptr, 0, 0) > 0) {
                                   TranslateMessage(&msg);
                                   DispatchMessageW(&msg);
                                   more = !self->stopping_.load(std::memory_order_acquire);
                               }
                               if (self->hook_ != nullptr) {
                                   UnhookWinEvent(static_cast<HWINEVENTHOOK>(self->hook_));
                                   self->hook_ = nullptr;
                               }
                               active_ = nullptr;
                               return 0;
                           },
                           this, 0, nullptr);
    if (thread_ == nullptr) {
        return ErrorFromWin32(GetLastError(), ErrorCode::InternalState,
                              L"Internal error tracking the recorded application's windows.",
                              L"CreateThread for PopupTracker failed");
    }
    log::Info("WindowManagement", "POPUP_TRACKING_STARTED", L"",
              L"Tracking new windows of the target process (FR-029).");
    return Result<void>{};
}

void PopupTracker::Stop() {
    if (thread_ == nullptr) return;
    stopping_.store(true, std::memory_order_release);
    // Break the message wait so the pump observes the stop flag promptly.
    PostThreadMessageW(GetThreadId(thread_), WM_QUIT, 0, 0);
    WaitForSingleObject(thread_, 3000);
    CloseHandle(thread_);
    thread_ = nullptr;
    stopping_.store(false);
}

VOID CALLBACK PopupTracker::WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject,
                                         LONG idChild, DWORD threadId, DWORD eventTime) {
    UNREFERENCED_PARAMETER(hook);
    UNREFERENCED_PARAMETER(event);
    UNREFERENCED_PARAMETER(idChild);
    UNREFERENCED_PARAMETER(threadId);
    UNREFERENCED_PARAMETER(eventTime);
    auto* self = active_;
    if (self == nullptr) return;
    if (idObject != OBJID_WINDOW) return;
    self->OnWindowShown(hwnd);
}

void PopupTracker::OnWindowShown(HWND hwnd) {
    if (hwnd == nullptr || hwnd == target_) return;
    if (!IsWindow(hwnd)) return;

    PopupCandidate c;
    c.visible = IsWindowVisible(hwnd) != FALSE;
    if (!c.visible) return;
    c.topLevel = GetAncestor(hwnd, GA_ROOT) == hwnd;
    if (!c.topLevel) return;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    c.sameProcess = pid == targetPid_;
    if (!c.sameProcess) return;

    c.mainFrame = hwnd == target_;
    c.hasCaption = (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) != 0;
    c.isToolWindow = (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0;
    c.cloaked = IsCloaked(hwnd);
    c.ownedByTarget = GetWindow(hwnd, GW_OWNER) == target_;
    wchar_t title[256] = {};
    const int len = GetWindowTextW(hwnd, title, 256);
    c.emptyTitle = len == 0;
    c.alreadyOnVirtual =
        MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) ==
        MonitorFromRect(&virtualWorkArea_, MONITOR_DEFAULTTONEAREST);
    // Windows already on the virtual output need no relocation; the decision
    // rule also refuses them.

    if (!ShouldRelocatePopup(c)) return;

    // Fit into the virtual work area, centered, never activating or
    // re-ordering (no focus steal — T100).
    const long areaW = virtualWorkArea_.right - virtualWorkArea_.left;
    const long areaH = virtualWorkArea_.bottom - virtualWorkArea_.top;
    RECT rc = {};
    GetWindowRect(hwnd, &rc);
    long w = rc.right - rc.left;
    long h = rc.bottom - rc.top;
    w = std::clamp(w, 200L, areaW > 0 ? areaW : w);
    h = std::clamp(h, 120L, areaH > 0 ? areaH : h);
    const long x = virtualWorkArea_.left + (areaW > w ? (areaW - w) / 2 : 0);
    const long y = virtualWorkArea_.top + (areaH > h ? (areaH - h) / 2 : 0);
    SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    relocated_.fetch_add(1, std::memory_order_relaxed);
    log::Info("WindowManagement", "POPUP_RELOCATED", L"",
              L"New target-process window moved to the virtual display.");
}

} // namespace vsr::window
