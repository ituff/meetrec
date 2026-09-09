#include "TopologyService.h"
#include "VirtualDisplayIdentity.h"

#include <Infrastructure/Log.h>
#include <Infrastructure/Strings.h>

#include <highlevelmonitorconfigurationapi.h>
#include <shellscalingapi.h>
#include <winuser.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shcore.lib")

namespace vsr::display {

namespace {

std::wstring GetTargetFriendlyName(const DISPLAYCONFIG_PATH_INFO& path) {
    DISPLAYCONFIG_TARGET_DEVICE_NAME targetName = {};
    targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    targetName.header.size = sizeof(targetName);
    targetName.header.adapterId = path.targetInfo.adapterId;
    targetName.header.id = path.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&targetName.header) == ERROR_SUCCESS) {
        return targetName.monitorFriendlyDeviceName;
    }
    return {};
}

std::wstring GetSourceGdiName(const DISPLAYCONFIG_PATH_INFO& path) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = {};
    sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    sourceName.header.size = sizeof(sourceName);
    sourceName.header.adapterId = path.sourceInfo.adapterId;
    sourceName.header.id = path.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS) {
        return sourceName.viewGdiDeviceName;
    }
    return {};
}

struct EnumContext {
    std::vector<MonitorDescriptor>* monitors;
    Error* error;
};

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdc, LPRECT rect, LPARAM lParam) {
    UNREFERENCED_PARAMETER(hdc);
    UNREFERENCED_PARAMETER(rect);
    auto* ctx = reinterpret_cast<EnumContext*>(lParam);

    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(hMonitor, &info)) {
        if (ctx->error != nullptr && ctx->error->code == ErrorCode::None) {
            *ctx->error = ErrorFromWin32(GetLastError(), ErrorCode::InternalState,
                                         L"Could not query a display.",
                                         L"GetMonitorInfoW failed");
        }
        return TRUE; // keep enumerating
    }

    MonitorDescriptor d;
    d.handle = hMonitor;
    d.monitorRect = info.rcMonitor;
    d.workRect = info.rcWork;
    d.gdiDeviceName = info.szDevice;
    d.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;

    UINT dpiX = 96;
    UINT dpiY = 96;
    if (SUCCEEDED(GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
        d.dpiX = dpiX;
        d.dpiY = dpiY;
    }

    // Resolve the friendly monitor name via the display path that owns this
    // GDI source (stable identity, not ordinal — FR-013).
    UINT pathCount = 0;
    UINT modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) ==
            ERROR_SUCCESS &&
        pathCount > 0) {
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount,
                               modes.data(), nullptr) == ERROR_SUCCESS) {
            paths.resize(pathCount);
            for (const auto& path : paths) {
                if (EqualsNoCase(GetSourceGdiName(path), info.szDevice)) {
                    d.friendlyName = GetTargetFriendlyName(path);
                    break;
                }
            }
        }
    }

    d.stableId = d.friendlyName.empty() ? d.gdiDeviceName : d.friendlyName;
    d.virtualRecordingDisplay = MatchesVirtualMonitorName(d.friendlyName);
    ctx->monitors->push_back(std::move(d));
    return TRUE;
}

} // namespace

std::vector<MonitorDescriptor> TopologyService::EnumerateMonitors(Error* error) {
    std::vector<MonitorDescriptor> monitors;
    EnumContext ctx{&monitors, error};
    if (error != nullptr) {
        *error = Error{};
        error->code = ErrorCode::None;
    }
    if (!EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc,
                             reinterpret_cast<LPARAM>(&ctx))) {
        // EnumDisplayMonitors returns FALSE only if the callback stopped it.
    }
    return monitors;
}

Result<MonitorDescriptor> TopologyService::FindVirtualMonitor() {
    Error err;
    const auto monitors = EnumerateMonitors(&err);
    if (monitors.empty() && err.code != ErrorCode::None) {
        return err;
    }
    for (const auto& m : monitors) {
        if (m.virtualRecordingDisplay) {
            return m;
        }
    }
    return Error(ErrorCode::DisplayNotFound,
                 L"The virtual recording display is not active. Start recording from the app so "
                 L"it can create the display, or reinstall the virtual display driver.",
                 L"No active monitor matched the virtual display identity",
                 S_OK, true);
}

Result<void> TopologyService::VerifyVirtualMonitorActive() {
    auto result = FindVirtualMonitor();
    if (!result) return result.TakeError();
    const auto& m = result.Value();
    // Extended-desktop participation: non-degenerate desktop rectangle.
    const long w = m.monitorRect.right - m.monitorRect.left;
    const long h = m.monitorRect.bottom - m.monitorRect.top;
    if (w <= 0 || h <= 0) {
        return Error(ErrorCode::DisplayDisconnected,
                     L"The virtual display lost its active mode. Reconnect it from the app and "
                     L"try again.",
                     L"Virtual monitor has a degenerate desktop rectangle", S_OK, true);
    }
    return Result<void>{};
}

TopologyService::TopologySummary TopologyService::Summarize() {
    TopologySummary summary;
    for (const auto& m : EnumerateMonitors()) {
        TopologySummary::Entry e;
        e.friendlyName = m.friendlyName;
        e.gdiDeviceName = m.gdiDeviceName;
        e.x = m.monitorRect.left;
        e.y = m.monitorRect.top;
        e.w = m.monitorRect.right - m.monitorRect.left;
        e.h = m.monitorRect.bottom - m.monitorRect.top;
        e.dpiX = m.dpiX;
        e.isVirtual = m.virtualRecordingDisplay;
        e.primary = m.primary;
        summary.monitors.push_back(std::move(e));
    }
    return summary;
}

class TopologyWatcher::Impl {
public:
    static constexpr wchar_t kWindowClass[] = L"VsrTopologyWatcherWnd";
    static constexpr UINT kDebounceMs = 500;
    static constexpr UINT kDebounceTimer = 1;

    Result<void> Start(Callback callback) {
        callback_ = std::move(callback);

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &Impl::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kWindowClass;
        RegisterClassExW(&wc);

        thread_ = CreateThread(nullptr, 0, &Impl::ThreadProc, this, 0, nullptr);
        if (thread_ == nullptr) {
            return ErrorFromWin32(GetLastError(), ErrorCode::InternalState,
                                  L"Internal error watching display changes.",
                                  L"CreateThread for TopologyWatcher failed");
        }
        return Result<void>{};
    }

    void Stop() {
        if (hwnd_ != nullptr) {
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        }
        if (thread_ != nullptr) {
            WaitForSingleObject(thread_, 5000);
            CloseHandle(thread_);
            thread_ = nullptr;
        }
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg) {
            case WM_DISPLAYCHANGE:
                if (self != nullptr) {
                    SetTimer(hwnd, kDebounceTimer, kDebounceMs, nullptr);
                }
                return 0;
            case WM_TIMER:
                if (self != nullptr && wp == kDebounceTimer) {
                    KillTimer(hwnd, kDebounceTimer);
                    if (self->callback_) self->callback_();
                }
                return 0;
            case WM_DESTROY:
                KillTimer(hwnd, kDebounceTimer);
                return 0;
            default:
                return DefWindowProcW(hwnd, msg, wp, lp);
        }
    }

    static DWORD CALLBACK ThreadProc(LPVOID param) {
        auto* self = static_cast<Impl*>(param);
        HWND hwnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kWindowClass,
                                    L"VsrTopologyWatcher", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                    GetModuleHandleW(nullptr), nullptr);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return 0;
    }

    Callback callback_;
    HWND hwnd_ = nullptr;
    HANDLE thread_ = nullptr;
};

constexpr wchar_t TopologyWatcher::Impl::kWindowClass[];

TopologyWatcher::TopologyWatcher() = default;
TopologyWatcher::~TopologyWatcher() { Stop(); }

Result<void> TopologyWatcher::Start(Callback callback) {
    impl_ = std::make_unique<Impl>();
    return impl_->Start(std::move(callback));
}

void TopologyWatcher::Stop() {
    if (impl_) {
        impl_->Stop();
        impl_.reset();
    }
}

} // namespace vsr::display
