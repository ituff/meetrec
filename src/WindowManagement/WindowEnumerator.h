// Virtual Screen Recorder - window enumeration and selection model (FR-020,
// FR-021, FR-022, FR-030; T050).
//
// Enumerates eligible top-level windows of the current interactive session:
// visible, root, unowned, non-cloaked, non-tool windows with non-empty
// titles. The recorder's own windows and usual shell surfaces are excluded.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace vsr::window {

struct WindowInfo {
    HWND hwnd = nullptr;
    DWORD pid = 0;
    std::wstring exeName; // executable display name, e.g. "notepad.exe"
    std::wstring title;   // sanitized for display only; never logged verbatim
    HICON icon = nullptr; // ownership transfers to the receiver
};

// Pure eligibility predicate (unit-testable); the classifier consumes the
// same data the enumerator collects.
struct WindowEligibilityData {
    bool visible = false;
    bool hasOwner = false;
    bool isToolWindow = false;
    bool cloaked = false;
    bool emptyTitle = false;
    bool ownedByRecorder = false;
    bool shellSurface = false;
};

bool IsWindowEligible(const WindowEligibilityData& data) noexcept;

class WindowEnumerator {
public:
    WindowEnumerator();

    // Returns the current eligible window list. `ownProcessId` is excluded.
    // Refresh keeps previously returned HICONs valid: icons are freshly
    // loaded per call and owned by the caller.
    std::vector<WindowInfo> Enumerate();

    // Rechecks a specific hwnd and reports whether it still belongs to
    // `expectedPid` (stale-HWND reuse guard, FR-028 support). A pid of 0 only
    // checks window existence.
    static bool IsTargetAlive(HWND hwnd, DWORD expectedPid);

    // Executable display name for a pid; "<unknown>" when unavailable.
    static std::wstring ExecutableNameForPid(DWORD pid);

private:
    DWORD ownProcessId_;
};

} // namespace vsr::window
