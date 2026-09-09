#include "WindowEnumerator.h"

#include <dwmapi.h>
#include <psapi.h>
#include <shellapi.h>

#include <Infrastructure/Log.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "psapi.lib")

namespace vsr::window {

bool IsWindowEligible(const WindowEligibilityData& data) noexcept {
    if (!data.visible) return false;
    if (data.hasOwner) return false;
    if (data.isToolWindow) return false;
    if (data.cloaked) return false;
    if (data.emptyTitle) return false;
    if (data.ownedByRecorder) return false;
    if (data.shellSurface) return false;
    return true;
}

namespace {

struct EnumData {
    DWORD ownProcessId;
    std::vector<WindowInfo>* out;
};

bool IsShellSurface(HWND hwnd) {
    wchar_t className[64] = {};
    if (GetClassNameW(hwnd, className, 64) == 0) return false;
    const wchar_t* shellClasses[] = {
        L"Progman",        L"WorkerW",          L"Shell_TrayWnd",
        L"Shell_SecondaryTrayWnd", L"SHELLDLL_DefView", L"SysListView32",
        L"Windows.UI.Core.CoreWindow",          L"ApplicationFrameWindow", // host process; real apps appear as children
        L"Windows.UI.Composition.DesktopWindowContentBridge",
        L"XamlExplorerHostIslandWindow",
        L"NotifyIconOverflowWindow", L"SysFader", L"MsgrIMEWindowClass", L"CTVScriptClass",
    };
    for (const auto* c : shellClasses) {
        if (lstrcmpW(className, c) == 0) return true;
    }
    return false;
}

std::wstring GetExeNameForPid(DWORD pid) {
    std::wstring name = L"<unknown>";
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process != nullptr) {
        wchar_t path[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(process, 0, path, &size) && size > 0) {
            const std::wstring full(path);
            const size_t slash = full.find_last_of(L"\\/");
            name = slash == std::wstring::npos ? full : full.substr(slash + 1);
        }
        CloseHandle(process);
    }
    return name;
}HICON LoadWindowIcon(HWND hwnd) {
    HICON icon = nullptr;
    SendMessageTimeoutW(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 200,
                        reinterpret_cast<PDWORD_PTR>(&icon));
    if (icon == nullptr) {
        SendMessageTimeoutW(hwnd, WM_GETICON, ICON_SMALL, 0, SMTO_ABORTIFHUNG, 200,
                            reinterpret_cast<PDWORD_PTR>(&icon));
    }
    if (icon == nullptr) {
        SendMessageTimeoutW(hwnd, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG, 200,
                            reinterpret_cast<PDWORD_PTR>(&icon));
    }
    if (icon == nullptr) {
        icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICON));
    }
    if (icon == nullptr) {
        icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICONSM));
    }
    if (icon == nullptr) {
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    return icon;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* data = reinterpret_cast<EnumData*>(lParam);

    WindowEligibilityData d;
    d.visible = IsWindowVisible(hwnd) != FALSE;
    if (!d.visible) return TRUE;

    // Root top-level window only (GA_ROOT ancestor check).
    if (GetAncestor(hwnd, GA_ROOT) != hwnd) return TRUE;

    d.hasOwner = GetWindow(hwnd, GW_OWNER) != nullptr;

    d.isToolWindow =
        (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0;

    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    d.cloaked = cloaked != FALSE;

    wchar_t title[512] = {};
    const int len = GetWindowTextW(hwnd, title, 512);
    d.emptyTitle = len == 0;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    d.ownedByRecorder = pid == data->ownProcessId;
    d.shellSurface = IsShellSurface(hwnd);

    if (!IsWindowEligible(d)) return TRUE;

    // Cloak detection for UWP hosted frames also covers width/height zero.
    RECT rc = {};
    if (!GetWindowRect(hwnd, &rc) || (rc.right - rc.left) <= 0 || (rc.bottom - rc.top) <= 0) {
        return TRUE;
    }

    WindowInfo info;
    info.hwnd = hwnd;
    info.pid = pid;
    info.title = title;
    info.exeName = GetExeNameForPid(pid);
    info.icon = LoadWindowIcon(hwnd);
    data->out->push_back(std::move(info));
    return TRUE;
}

} // namespace

WindowEnumerator::WindowEnumerator() : ownProcessId_(GetCurrentProcessId()) {}

std::vector<WindowInfo> WindowEnumerator::Enumerate() {
    std::vector<WindowInfo> out;
    EnumData data{ownProcessId_, &out};
    EnumWindows(&EnumWindowsProc, reinterpret_cast<LPARAM>(&data));
    return out;
}

bool WindowEnumerator::IsTargetAlive(HWND hwnd, DWORD expectedPid) {
    if (hwnd == nullptr || !IsWindow(hwnd)) return false;
    if (expectedPid == 0) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == expectedPid;
}

std::wstring WindowEnumerator::ExecutableNameForPid(DWORD pid) {
    return GetExeNameForPid(pid);
}

} // namespace vsr::window
