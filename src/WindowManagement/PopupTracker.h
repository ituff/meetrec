// Virtual Screen Recorder - owned-popup relocation tracker (FR-029, T100).
//
// While a recording is active, new top-level windows that belong to the
// target process (dialogs, popouts) are relocated onto the virtual display so
// they stay visible in the recording. Relocation NEVER activates or
// re-orders windows and never touches windows of other processes.
//
// The decision logic is pure and unit-tested; the tracker drives it from a
// WinEvent hook on a dedicated message-pump thread.
#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>

#include <Infrastructure/Error.h>
#include <Infrastructure/Result.h>

namespace vsr::window {

// Pure relocation decision (unit-tested).
struct PopupCandidate {
    bool visible = false;          // IsWindowVisible
    bool topLevel = false;         // GA_ROOT ancestor is itself
    bool hasCaption = false;       // WS_CAPTION styled
    bool isToolWindow = false;     // WS_EX_TOOLWINDOW
    bool cloaked = false;          // DWMWA_CLOAKED
    bool sameProcess = false;      // belongs to the target PID
    bool ownedByTarget = false;    // GW_OWNER chain reaches the recorded window
    bool emptyTitle = false;
    bool alreadyOnVirtual = false; // MonitorFromWindow == virtual monitor
    bool mainFrame = false;        // the recorded window itself
};

bool ShouldRelocatePopup(const PopupCandidate& candidate) noexcept;

class PopupTracker {
public:
    PopupTracker() = default;
    ~PopupTracker();

    PopupTracker(const PopupTracker&) = delete;
    PopupTracker& operator=(const PopupTracker&) = delete;

    // Tracks the target process until Stop. `ownerHwnd` is the recorded
    // window; new eligible windows are placed inside `virtualWorkArea`.
    Result<void> Start(HWND targetHwnd, const RECT& virtualWorkArea);
    void Stop();

    // Test/inspection counters.
    std::uint64_t RelocatedCount() const noexcept { return relocated_.load(std::memory_order_relaxed); }

private:
    static VOID CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject,
                                      LONG idChild, DWORD threadId, DWORD eventTime);
    void OnWindowShown(HWND hwnd);

    static PopupTracker* active_; // hook callbacks arrive on the hook thread

    HWND target_ = nullptr;
    DWORD targetPid_ = 0;
    RECT virtualWorkArea_{};
    void* hook_ = nullptr;   // HWINEVENTHOOK
    HANDLE thread_ = nullptr;
    std::atomic<bool> stopping_{false};
    std::atomic<std::uint64_t> relocated_{0};
};

} // namespace vsr::window
