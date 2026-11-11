#include "MainWindow.h"

#include <windowsx.h>

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <vector>

#include <Application/Diagnostics.h>
#include <Application/OutputFile.h>
#include <Encoder/AudioCapture.h>
#include <Encoder/EncoderProbe.h>
#include <Encoder/VideoWriterFactory.h>
#include <Infrastructure/Error.h>
#include <Infrastructure/Log.h>
#include <Infrastructure/Paths.h>
#include <Infrastructure/Settings.h>
#include <Infrastructure/Strings.h>
#include <Update/UpdateApplier.h>
#include <Update/UpdateChecker.h>

#include "AppMenu.h"
#include "Application.h"
#include "CompressWindow.h"
#include "PreviewPanel.h"
#include "SettingsWindow.h"
#include "ToastWindow.h"
#include "UpdateWindow.h"
#include "Version.h"
#include "WindowPicker.h"

#pragma comment(lib, "comctl32.lib")

namespace vsr::app {

namespace {

constexpr UINT WM_APP_ACTIVATE = WM_APP + 1;
constexpr UINT WM_APP_TRAY = WM_APP + 2;
constexpr UINT WM_APP_BROWSE_RESULT = WM_APP + 3;
constexpr UINT WM_APP_THUMBNAIL = WM_APP + 4;
constexpr UINT WM_APP_SETTINGS_CHANGED = WM_APP + 5;
constexpr UINT WM_APP_PICKER_RESULT = WM_APP + 6;
constexpr UINT WM_APP_UPDATE_CHECK_RESULT = WM_APP + 7;
constexpr int kAppIconResource = 1;

constexpr UINT kCmdQualityBase = 2000;
constexpr UINT kCmdDeviceBase = 2100;
constexpr UINT kCmdMoreSettings = 2201;
constexpr UINT kCmdMoreHelp = 2202;
constexpr UINT kCmdMoreAbout = 2203;
constexpr UINT kCmdMoreDiagnostics = 2204;
constexpr UINT kCmdMoreExit = 2205;
constexpr UINT kCmdMoreCompress = 2206;
constexpr UINT kCmdMoreUpdate = 2207;
constexpr UINT kCmdTrayOpen = 2301;
constexpr UINT kCmdTrayToggle = 2302;
constexpr UINT kCmdTrayStop = 2303;
constexpr UINT kCmdTraySettings = 2304;
constexpr UINT kCmdTrayHelp = 2305;
constexpr UINT kCmdTrayExit = 2306;
constexpr UINT kCmdTrayCompress = 2307;
constexpr UINT kCmdTrayUpdate = 2308;

struct UpdateCheckPayload {
    bool manual = false;
    bool ok = false;
    bool available = false;
    update::ReleaseInfo release;
    std::wstring error;
};

Gdiplus::RectF RectFOf(const RECT& rect) {
    return Gdiplus::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top),
                          static_cast<float>(rect.right - rect.left),
                          static_cast<float>(rect.bottom - rect.top));
}

bool Contains(const Gdiplus::RectF& rect, POINT point) {
    return static_cast<float>(point.x) >= rect.X &&
           static_cast<float>(point.x) < rect.X + rect.Width &&
           static_cast<float>(point.y) >= rect.Y &&
           static_cast<float>(point.y) < rect.Y + rect.Height;
}

Gdiplus::RectF Inflate(const Gdiplus::RectF& rect, float dx, float dy) {
    return Gdiplus::RectF(rect.X - dx, rect.Y - dy, rect.Width + dx * 2.0f,
                          rect.Height + dy * 2.0f);
}

Gdiplus::RectF Union(const Gdiplus::RectF& a, const Gdiplus::RectF& b) {
    const float left = std::min(a.X, b.X);
    const float top = std::min(a.Y, b.Y);
    const float right = std::max(a.GetRight(), b.GetRight());
    const float bottom = std::max(a.GetBottom(), b.GetBottom());
    return Gdiplus::RectF(left, top, right - left, bottom - top);
}

std::wstring DefaultOutputDirectory() {
    std::filesystem::path videos;
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &raw)) && raw != nullptr) {
        videos = raw;
        CoTaskMemFree(raw);
    } else {
        wchar_t profile[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) > 0) {
            videos = std::filesystem::path(profile) / L"Videos";
        } else {
            videos = std::filesystem::path(L"C:\\");
        }
    }
    videos /= L"MeetRec";
    std::error_code ec;
    std::filesystem::create_directories(videos, ec);
    return videos.wstring();
}

std::wstring ParentFolder(const std::wstring& path) {
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) return path;
    return parent.wstring();
}

std::wstring BaseName(const std::wstring& path) {
    return std::filesystem::path(path).filename().wstring();
}

bool IsFileUsable(const std::wstring& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) &&
           std::filesystem::file_size(path, ec) > 0;
}

std::uint64_t FileSize(const std::wstring& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    return ec ? 0 : static_cast<std::uint64_t>(size);
}

bool IsRetryableAfterStartFailure(ErrorCode code) {
    switch (code) {
        case ErrorCode::OutputNotWritable:
        case ErrorCode::OutputDiskLow:
        case ErrorCode::EncoderUnavailable:
        case ErrorCode::InitD3dFailed:
        case ErrorCode::InitDriverMissing:
        case ErrorCode::InitOsUnsupported:
            return false;
        default:
            return true;
    }
}

std::wstring ExeDisplayName(const std::wstring& exe) {
    if (exe.empty() || exe == L"<unknown>") return exe;
    return exe;
}

} // namespace

MainWindow::MainWindow(Coordinator& coordinator) : coordinator_(coordinator) {}

MainWindow::~MainWindow() {
    if (levelMonitor_ != nullptr) {
        levelMonitor_->Stop();
        levelMonitor_.reset();
    }
    if (settings_) {
        settings_.reset();
    }
    picker_.reset();
    toast_.reset();
    preview_.reset();
    if (stopOp_.joinable()) stopOp_.join();
    if (memDc_ != nullptr) {
        DeleteDC(memDc_);
        memDc_ = nullptr;
    }
    if (memBmp_ != nullptr) {
        DeleteObject(memBmp_);
        memBmp_ = nullptr;
    }
    for (const auto& info : windows_) {
        if (info.icon != nullptr) DestroyIcon(info.icon);
    }
    windows_.clear();
    targetIcon_.reset();
    fonts_.Destroy();
}

bool MainWindow::Create() {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &MainWindow::WndProcThunk;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kMainWindowClassName;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(kAppIconResource));
    wc.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(kAppIconResource));
    RegisterClassExW(&wc);

    dpi_ = GetDpiForSystem();
    const int width = ui::Scale(780, dpi_);
    const int height = ui::Scale(540, dpi_);
    hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, kMainWindowClassName,
                            i18n::Text(i18n::Str::AppName),
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                            CW_USEDEFAULT, width, height, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) return false;

    // Windows 11 rounded corners (ignored on Windows 10); the drawing is
    // fully ours, so the system caption is removed in WM_NCCALCSIZE.
    DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    BOOL dark = FALSE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    MARGINS margins = {1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(hwnd_, &margins);

    OnCreate();

    // Center on the work area of the monitor the cursor is on.
    RECT window = {};
    GetWindowRect(hwnd_, &window);
    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitorInfo)) {
        const int w = window.right - window.left;
        const int h = window.bottom - window.top;
        const int x = monitorInfo.rcWork.left +
                      ((monitorInfo.rcWork.right - monitorInfo.rcWork.left) - w) / 2;
        const int y = monitorInfo.rcWork.top +
                      ((monitorInfo.rcWork.bottom - monitorInfo.rcWork.top) - h) / 2;
        SetWindowPos(hwnd_, nullptr, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return true;
}

void MainWindow::ShowAndActivate() {
    if (hwnd_ == nullptr) return;
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    else ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
}

void MainWindow::RefreshUi() {
    OnTimer();
}

void MainWindow::OnCreate() {
    dpi_ = GetDpiForWindow(hwnd_);
    thumbGeneration_ = std::make_shared<std::atomic<int>>(0);

    LoadSettings();
    RecreateFonts();

    preview_ = std::make_unique<PreviewPanel>(coordinator_);
    preview_->Create(hwnd_, GetModuleHandleW(nullptr));
    preview_->SetEnabled(showPreview_);
    if (preview_->Hwnd() != nullptr) ShowWindow(preview_->Hwnd(), SW_HIDE);

    lastSnapshot_ = coordinator_.GetStatus();
    previousState_ = lastSnapshot_.state;

    RefreshWindowList();
    UpdateLayout();

    tray_.Create(hwnd_, WM_APP_TRAY);
    tray_.Update(lastSnapshot_);
    SetTimer(hwnd_, timerId_, 200, nullptr);
    if (autoCheckUpdates_) nextUpdateCheckTick_ = GetTickCount64() + 6000;

    activateMessage_ = RegisterWindowMessageW(L"VsrActivateExistingInstance");
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    UpdateWindowTitle();
    PersistSettings();
}

void MainWindow::UpdateWindowTitle() {
    if (hwnd_ != nullptr) SetWindowTextW(hwnd_, i18n::Text(i18n::Str::AppName));
}

void MainWindow::RecreateFonts() {
    fonts_.Create(dpi_, i18n::CurrentLanguage());
}

void MainWindow::OnDpiChanged(UINT newDpi) {
    dpi_ = newDpi;
    RecreateFonts();
    UpdateLayout();
    InvalidateAll();
}

LRESULT CALLBACK MainWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self != nullptr ? self->WndProc(hwnd, msg, wp, lp)
                           : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCCALCSIZE: {
            if (wp == TRUE) {
                auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                if (IsZoomed(hwnd)) {
                    MONITORINFO info = {};
                    info.cbSize = sizeof(info);
                    if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST),
                                        &info)) {
                        params->rgrc[0] = info.rcWork;
                    }
                }
            }
            return 0;
        }
        case WM_NCACTIVATE:
            return TRUE;
        case WM_NCPAINT:
            return 0;
        case WM_NCHITTEST: {
            POINT screen = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (!IsZoomed(hwnd)) {
                POINT client = screen;
                ScreenToClient(hwnd, &client);
                RECT rc = {};
                GetClientRect(hwnd, &rc);
                const int edge = ui::Scale(6, dpi_);
                const bool left = client.x < edge;
                const bool right = client.x >= rc.right - edge;
                const bool top = client.y < edge;
                const bool bottom = client.y >= rc.bottom - edge;
                if (top && left) return HTTOPLEFT;
                if (top && right) return HTTOPRIGHT;
                if (bottom && left) return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left) return HTLEFT;
                if (right) return HTRIGHT;
                if (top) return HTTOP;
                if (bottom) return HTBOTTOM;
            }
            POINT client = screen;
            ScreenToClient(hwnd, &client);
            if (Contains(layout_.titleBar, client) &&
                !Contains(layout_.settings, client) && !Contains(layout_.more, client) &&
                !Contains(layout_.minimize, client) && !Contains(layout_.maximize, client) &&
                !Contains(layout_.close, client)) {
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            if (info != nullptr) {
                const UINT windowDpi = hwnd != nullptr ? GetDpiForWindow(hwnd) : 96;
                info->ptMinTrackSize.x = ui::Scale(760, windowDpi);
                const int minHeight = page_ == Page::Main ? 500
                                       : page_ == Page::Result ? 300
                                                               : 340;
                info->ptMinTrackSize.y = ui::Scale(minHeight, windowDpi);
            }
            return 0;
        }
        case WM_DPICHANGED: {
            const UINT newDpi = HIWORD(wp);
            auto* suggested = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            OnDpiChanged(newDpi);
            return 0;
        }
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) {
                UpdateLayout();
                InvalidateAll();
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC dc = BeginPaint(hwnd, &ps);
            Paint(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_PRINTCLIENT: {
            Paint(reinterpret_cast<HDC>(wp));
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!trackingMouse_) {
                TRACKMOUSEEVENT track = {};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = hwnd;
                TrackMouseEvent(&track);
                trackingMouse_ = true;
            }
            POINT point = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (draggingSplitter_) {
                const float minLeft = ui::ScaleF(380.0f, dpi_);
                const float minRight = ui::ScaleF(260.0f, dpi_);
                // Move the divider with the pointer: left width follows the
                // drag, so dragging right grows the settings column and
                // dragging left enlarges the preview.
                float left = splitterDragStartLeft_ + (point.x - splitterDragStartX_);
                left = std::clamp(left, minLeft,
                                  std::max(minLeft, splitterDragAvailable_ - minRight));
                previewRatio_ = (splitterDragAvailable_ - left) / splitterDragAvailable_;
                UpdateLayout();
                InvalidateAll();
                return 0;
            }
            SetHover(HitTest(point));
            return 0;
        }
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            SetHover(Hit::None);
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                if (draggingSplitter_) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
                POINT point = {};
                GetCursorPos(&point);
                ScreenToClient(hwnd, &point);
                const Hit hit = HitTest(point);
                if (hit == Hit::Splitter) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                } else {
                    SetCursor(hit != Hit::None && IsHitEnabled(hit)
                                  ? LoadCursorW(nullptr, IDC_HAND)
                                  : LoadCursorW(nullptr, IDC_ARROW));
                }
                return TRUE;
            }
            break;
        case WM_LBUTTONDBLCLK: {
            POINT point = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (HitTest(point) == Hit::Splitter) {
                previewRatio_ = 0.40f;
                UpdateLayout();
                PersistSettings();
                InvalidateAll();
                return 0;
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            POINT point = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            const Hit hit = HitTest(point);
            if (hit == Hit::Splitter && IsHitEnabled(hit)) {
                draggingSplitter_ = true;
                splitterDragStartX_ = point.x;
                splitterDragStartLeft_ = layoutAvailableWidth_ - layoutRightWidth_;
                splitterDragAvailable_ = layoutAvailableWidth_;
                focus_ = Hit::Splitter;
                hover_ = Hit::Splitter;
                keyboardFocus_ = false;
                SetCapture(hwnd);
                InvalidateAll();
                return 0;
            }
            if (hit != Hit::None && IsHitEnabled(hit)) {
                pressed_ = hit;
                focus_ = hit;
                keyboardFocus_ = false;
                SetCapture(hwnd);
                InvalidateAll();
            } else {
                focus_ = Hit::None;
                keyboardFocus_ = false;
                InvalidateAll();
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (draggingSplitter_) {
                draggingSplitter_ = false;
                if (GetCapture() == hwnd) ReleaseCapture();
                PersistSettings();
                InvalidateAll();
                return 0;
            }
            const bool hadCapture = GetCapture() == hwnd;
            // ReleaseCapture synchronously posts WM_CAPTURECHANGED, which
            // clears pressed_; remember the button before releasing.
            const Hit pressed = pressed_;
            pressed_ = Hit::None;
            if (hadCapture) ReleaseCapture();
            if (pressed == Hit::None) return 0;
            POINT point = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            const Hit hit = HitTest(point);
            if (hit == pressed && IsHitEnabled(pressed)) {
                Activate(pressed);
            } else {
                InvalidateAll();
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (draggingSplitter_) {
                draggingSplitter_ = false;
                PersistSettings();
            }
            pressed_ = Hit::None;
            InvalidateAll();
            return 0;
        case WM_KEYDOWN: {
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (wp == VK_TAB) {
                CycleFocus(shift);
                return 0;
            }
            if (wp == VK_RETURN || wp == VK_SPACE) {
                if (focus_ != Hit::None && IsHitEnabled(focus_)) {
                    Activate(focus_);
                } else if (lastSnapshot_.state == AppState::Ready && page_ == Page::Main) {
                    OnStartClicked();
                }
                return 0;
            }
            if (wp == VK_ESCAPE) {
                if (focus_ != Hit::None) {
                    focus_ = Hit::None;
                    keyboardFocus_ = false;
                    InvalidateAll();
                    return 0;
                }
                break;
            }
            if ((wp == VK_LEFT || wp == VK_RIGHT) && focus_ == Hit::Splitter) {
                const float minLeft = ui::ScaleF(380.0f, dpi_);
                const float minRight = ui::ScaleF(260.0f, dpi_);
                const float delta = ui::ScaleF(16.0f, dpi_);
                float left = layoutAvailableWidth_ - layoutRightWidth_ +
                             (wp == VK_LEFT ? -delta : delta);
                left = std::clamp(left, minLeft,
                                  std::max(minLeft, layoutAvailableWidth_ - minRight));
                previewRatio_ = (layoutAvailableWidth_ - left) / layoutAvailableWidth_;
                UpdateLayout();
                PersistSettings();
                InvalidateAll();
                return 0;
            }
            if ((wp == VK_UP || wp == VK_DOWN) && focus_ != Hit::None) {
                const int delta = wp == VK_UP ? -1 : 1;
                if (focus_ == Hit::Quality) {
                    SetQualityIndex((QualityIndex() + delta + 3) % 3);
                    PersistSettings();
                    InvalidateAll();
                    return 0;
                }
                if (focus_ == Hit::OutputDevice || focus_ == Hit::InputDevice) {
                    const bool output = focus_ == Hit::OutputDevice;
                    const auto& devices = output ? outputDevices_ : inputDevices_;
                    if (!devices.empty()) {
                        int index = (output ? outputSelection_ : inputSelection_) + delta;
                        if (index < 0) index = static_cast<int>(devices.size());
                        if (index > static_cast<int>(devices.size())) index = 0;
                        SelectAudioDevice(output, index == 0 ? L"" : devices[index - 1].id);
                        PersistSettings();
                        InvalidateAll();
                    }
                    return 0;
                }
            }
            if (wp == VK_F5) {
                RefreshWindowList();
                RefreshAudioDevices();
                InvalidateAll();
                return 0;
            }
            if (wp == VK_APPS) {
                ShowMoreMenu();
                return 0;
            }
            break;
        }
        case WM_MEASUREITEM:
            if (menu::HandleMeasureItem(reinterpret_cast<MEASUREITEMSTRUCT*>(lp), fonts_)) {
                return TRUE;
            }
            break;
        case WM_DRAWITEM:
            if (menu::HandleDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lp), fonts_)) {
                return TRUE;
            }
            break;
        case WM_TIMER:
            if (wp == timerId_) OnTimer();
            return 0;
        case WM_APP_BROWSE_RESULT: {
            browseRunning_.store(false);
            auto* picked = reinterpret_cast<std::wstring*>(lp);
            if (picked != nullptr) {
                if (!picked->empty()) {
                    outputDirectory_ = *picked;
                    PersistSettings();
                    InvalidateAll();
                }
                delete picked;
            }
            return 0;
        }
        case WM_APP_THUMBNAIL: {
            auto* payload = reinterpret_cast<ThumbnailPayload*>(lp);
            if (payload != nullptr) {
                const int generation =
                    thumbGeneration_ ? thumbGeneration_->load() : payload->generation;
                if (payload->generation == generation) {
                    thumbnailPending_ = false;
                    if (!payload->failed && !payload->bgra.empty()) {
                        thumbnailFailed_ = false;
                        if (preview_ != nullptr) {
                            preview_->SetStaticFrame(std::move(payload->bgra),
                                                     payload->width, payload->height);
                        }
                        if (page_ == Page::Result && resultThumb_.empty()) {
                            if (preview_ != nullptr) {
                                preview_->CopyLastFrame(resultThumb_, resultThumbWidth_,
                                                        resultThumbHeight_);
                            }
                        }
                        UpdateLayout();
                        InvalidateAll();
                    } else {
                        thumbnailFailed_ = true;
                        InvalidateAll();
                    }
                }
                delete payload;
            }
            return 0;
        }
        case WM_APP_SETTINGS_CHANGED:
            ApplySettingsFromStore();
            if (compressor_) compressor_->OnLanguageChanged();
            return 0;
        case WM_APP_UPDATE_CHECK_RESULT: {
            auto* payload = reinterpret_cast<UpdateCheckPayload*>(lp);
            if (payload == nullptr) return 0;
            updateCheckRunning_.store(false);
            lastUpdateCheckUnix_ = static_cast<long long>(std::time(nullptr));
            PersistSettings();
            if (!payload->ok) {
                log::Warning("Update", "CHECK_FAILED", L"", payload->error);
                if (payload->manual) {
                    const std::wstring body = i18n::Format(
                        i18n::Str::UpdateCheckFailedBody, {payload->error});
                    MessageBoxW(hwnd_, body.c_str(),
                                i18n::Text(i18n::Str::UpdateCheckFailedTitle),
                                MB_OK | MB_ICONWARNING);
                }
                delete payload;
                return 0;
            }
            if (!payload->available) {
                if (payload->manual) {
                    const std::wstring body = i18n::Format(
                        i18n::Str::UpdateLatestBody, {VSR_APP_VERSION_W});
                    MessageBoxW(hwnd_, body.c_str(),
                                i18n::Text(i18n::Str::UpdateLatestTitle),
                                MB_OK | MB_ICONINFORMATION);
                }
                delete payload;
                return 0;
            }
            updateAvailable_ = true;
            updateRelease_ = payload->release;
            updateVersion_ = Utf8ToWide(payload->release.version.ToString());
            log::Info("Update", "UPDATE_AVAILABLE", L"", updateVersion_);
            if (lastNotifiedUpdateVersion_ != payload->release.version.ToString()) {
                lastNotifiedUpdateVersion_ = payload->release.version.ToString();
                PersistSettings();
                pendingUpdateToast_ = true;
            }
            if (openUpdateWindowOnAvailable_) {
                openUpdateWindowOnAvailable_ = false;
                OpenUpdateWindow();
            } else if (payload->manual) {
                ShowUpdateDialog();
            }
            delete payload;
            return 0;
        }
        case WM_APP_PICKER_RESULT: {
            OnPickerResult(static_cast<int>(wp) - 1);
            return 0;
        }
        case WM_APP_ACTIVATE:
            ShowAndActivate();
            return 0;
        case WM_APP_TRAY: {
            if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
                ShowTrayMenu();
            } else if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) {
                ShowAndActivate();
            }
            return 0;
        }
        case WM_CLOSE: {
            const StatusSnapshot snap = coordinator_.GetStatus();
            const bool active = snap.state == AppState::Recording ||
                                snap.state == AppState::Paused ||
                                snap.state == AppState::Preparing;
            if (snap.state == AppState::Stopping) {
                MessageBoxW(hwnd, i18n::Text(i18n::Str::SavingCloseBlocked),
                            i18n::Text(i18n::Str::AppName), MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            if (active) {
                if (minimizeToTray_) {
                    ShowWindow(hwnd, SW_HIDE);
                    return 0;
                }
                const int choice = MessageBoxW(
                    hwnd, i18n::Text(i18n::Str::ConfirmCloseRecordingBody),
                    i18n::Text(i18n::Str::ConfirmCloseRecordingTitle),
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
                if (choice != IDYES) return 0;
                (void)coordinator_.Stop(StopReason::User);
            }
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd, timerId_);
            if (stopOp_.joinable()) stopOp_.join();
            if (levelMonitor_ != nullptr) {
                levelMonitor_->Stop();
                levelMonitor_.reset();
            }
            settings_.reset();
            picker_.reset();
            toast_.reset();
            preview_.reset();
            for (const auto& info : windows_) {
                if (info.icon != nullptr) DestroyIcon(info.icon);
            }
            windows_.clear();
            targetIcon_.reset();
            if (memDc_ != nullptr) {
                DeleteDC(memDc_);
                memDc_ = nullptr;
            }
            if (memBmp_ != nullptr) {
                DeleteObject(memBmp_);
                memBmp_ = nullptr;
            }
            memWidth_ = 0;
            memHeight_ = 0;
            tray_.Remove();
            PersistSettings();
            PostQuitMessage(0);
            return 0;
        default:
            if (activateMessage_ != 0 && msg == activateMessage_) {
                ShowAndActivate();
                return 0;
            }
            if (taskbarCreatedMessage_ != 0 && msg == taskbarCreatedMessage_) {
                tray_.Remove();
                tray_.Create(hwnd_, WM_APP_TRAY);
                tray_.Update(lastSnapshot_);
                return 0;
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void MainWindow::OnTimer() {
    ReapStopOperation();

    StatusSnapshot snap = coordinator_.GetStatus();
    lastSnapshot_ = snap;
    const AppState previous = previousState_;
    const bool wasActive = IsActiveState(previous);
    const bool isActive = IsActiveState(snap.state);

    if (snap.state == AppState::Recording) {
        lastActiveElapsedMs_ = std::max(lastActiveElapsedMs_, snap.activeElapsedMs);
    }

    float loopbackPeak = 0.0f;
    float micPeak = 0.0f;
    if (snap.state == AppState::Recording) {
        coordinator_.GetAudioLevels(loopbackPeak, micPeak);
    } else if (snap.state == AppState::Ready) {
        loopbackPeak = previewLoopbackLevel_.load(std::memory_order_relaxed);
        micPeak = previewMicLevel_.load(std::memory_order_relaxed);
    }
    auto smooth = [](float current, float target) {
        return target > current ? target : current * 0.82f;
    };
    systemLevel_ = systemAudio_ ? smooth(systemLevel_, loopbackPeak) : 0.0f;
    micLevel_ = microphone_ ? smooth(micLevel_, micPeak) : 0.0f;

    if (snap.state == AppState::Recording && previous != AppState::Recording &&
        previous != AppState::Paused) {
        ShowToast(i18n::Text(i18n::Str::ToastRecordingTitle), ActiveTargetName(),
                  ElapsedText(), false, true);
    }
    if (snap.state == AppState::Paused && previous != AppState::Paused && showNotification_) {
        ShowToast(i18n::Text(i18n::Str::ToastPausedTitle), ActiveTargetName(), ElapsedText(),
                  true, true);
    }

    if (wasActive && !isActive &&
        (snap.state == AppState::Ready || snap.state == AppState::Faulted)) {
        FinishRecording(snap);
    } else if (snap.state == AppState::Faulted && previous != AppState::Faulted &&
               page_ == Page::Main) {
        EnterErrorPage(snap);
    }

    previousState_ = snap.state;

    if (snap.state == AppState::Preparing || snap.state == AppState::Stopping) {
        spinnerAngle_ = std::fmod(spinnerAngle_ + 24.0f, 360.0f);
    }

    if (preview_ != nullptr && preview_->IsEnabled()) {
        const bool hadFrame = preview_->HasFrame();
        preview_->Tick();
        if (preview_->HasFrame()) {
            thumbnailPending_ = false;
            thumbnailFailed_ = false;
        }
        if (hadFrame != preview_->HasFrame()) UpdatePreviewChild();
    }
    // A PrintWindow request can block on a busy target; don't leave the
    // preview saying "preparing" forever.
    if (thumbnailPending_ && GetTickCount64() - thumbnailRequestTick_ > 3000) {
        thumbnailPending_ = false;
        thumbnailFailed_ = true;
        InvalidateAll();
    }

    if (previous != snap.state) {
        UpdateLayout();
        InvalidateAll();
    } else if (isActive || (page_ == Page::Main && snap.state == AppState::Faulted)) {
        InvalidateAll();
    }
    if (toast_ != nullptr && toastLiveElapsed_ && toast_->IsVisible()) {
        toast_->SetSecondLine(ElapsedText());
    }
    if (autoCheckUpdates_ && nextUpdateCheckTick_ != 0 &&
        GetTickCount64() >= nextUpdateCheckTick_ && !updateCheckRunning_.load()) {
        nextUpdateCheckTick_ = 0;
        const long long now = static_cast<long long>(std::time(nullptr));
        if (openUpdateWindowOnAvailable_ || lastUpdateCheckUnix_ == 0 ||
            now - lastUpdateCheckUnix_ >= 24 * 60 * 60) {
            StartUpdateCheck(false);
        }
    }
    if (pendingUpdateToast_ && snap.state == AppState::Ready && !updateVersion_.empty()) {
        pendingUpdateToast_ = false;
        ShowToast(i18n::Text(i18n::Str::UpdateToastTitle), updateVersion_,
                  i18n::Format(i18n::Str::UpdateToastBody, {updateVersion_}), false,
                  false);
    }
    RefreshLevelMonitor();
    UpdateTray();
}

void MainWindow::ReapStopOperation() {
    if (!stopOpDone_.load()) return;
    if (stopOp_.joinable()) stopOp_.join();
    stopOpRunning_.store(false);
    stopOpDone_.store(false);
    OnTimer();
}

void MainWindow::Paint(HDC targetDc) {
    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;

    if (memDc_ == nullptr || memWidth_ != width || memHeight_ != height) {
        if (memDc_ != nullptr) {
            DeleteDC(memDc_);
            memDc_ = nullptr;
        }
        if (memBmp_ != nullptr) {
            DeleteObject(memBmp_);
            memBmp_ = nullptr;
        }
        HDC screen = GetDC(hwnd_);
        memDc_ = CreateCompatibleDC(screen);
        memBmp_ = CreateCompatibleBitmap(screen, width, height);
        ReleaseDC(hwnd_, screen);
        if (memDc_ == nullptr || memBmp_ == nullptr) return;
        SelectObject(memDc_, memBmp_);
        memWidth_ = width;
        memHeight_ = height;
    }

    {
        Gdiplus::Graphics graphics(memDc_);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        Draw(graphics, width, height);
    }

    if (targetDc != nullptr) {
        BitBlt(targetDc, 0, 0, width, height, memDc_, 0, 0, SRCCOPY);
    }
}

void MainWindow::Draw(Gdiplus::Graphics& graphics, int width, int height) {
    ui::FillRect(graphics, Gdiplus::RectF(0, 0, static_cast<float>(width),
                                           static_cast<float>(height)),
                 ui::colors::Window());
    DrawTitleBar(graphics);
    if (page_ == Page::Main) {
        DrawMainPage(graphics);
        DrawFooter(graphics, lastSnapshot_);
    } else if (page_ == Page::Result) {
        DrawResultPage(graphics);
    } else {
        DrawErrorPage(graphics);
    }

    if (keyboardFocus_ && focus_ != Hit::None) {
        for (const HitTarget& hit : hits_) {
            if (hit.id == focus_) {
                ui::StrokeRoundRect(graphics, Inflate(hit.rect, ui::ScaleF(2.0f, dpi_),
                                                      ui::ScaleF(2.0f, dpi_)),
                                    ui::ScaleF(8.0f, dpi_), ui::colors::Primary(),
                                    ui::ScaleF(2.0f, dpi_));
                break;
            }
        }
    }
}

void MainWindow::DrawTitleBar(Gdiplus::Graphics& graphics) {
    ui::FillRect(graphics, layout_.titleBar, ui::colors::TitleBar());
    ui::DrawLine(graphics, 0.0f, layout_.titleBar.GetBottom() - 0.5f,
                 layout_.titleBar.GetRight(), layout_.titleBar.GetBottom() - 0.5f,
                 ui::colors::Divider(), 1.0f);

    ui::DrawLogo(graphics, layout_.logo, ui::ScaleF(7.0f, dpi_));
    const std::wstring appName = i18n::Text(i18n::Str::AppName);
    const Gdiplus::SizeF appSize =
        ui::MeasureText(graphics, fonts_, appName, ui::FontRole::Title);
    ui::DrawText(graphics, fonts_, appName,
                 Gdiplus::RectF(layout_.logo.GetRight() + ui::ScaleF(10.0f, dpi_),
                                layout_.appTitle.Y, appSize.Width + ui::ScaleF(4.0f, dpi_),
                                layout_.appTitle.Height),
                 ui::FontRole::Title, ui::colors::Text(), ui::Align::Near,
                 ui::VAlign::Center, false);
    const float taglineX =
        layout_.logo.GetRight() + ui::ScaleF(10.0f, dpi_) + appSize.Width +
        ui::ScaleF(14.0f, dpi_);
    const float taglineWidth = layout_.settings.X - taglineX - ui::ScaleF(12.0f, dpi_);
    if (taglineWidth > ui::ScaleF(80.0f, dpi_)) {
        ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::AppTagline),
                     Gdiplus::RectF(taglineX, layout_.appTitle.Y, taglineWidth,
                                    layout_.appTitle.Height),
                     ui::FontRole::Small, ui::colors::TextSecondary(), ui::Align::Near,
                     ui::VAlign::Center, true);
    }

    const bool settingsHover = hover_ == Hit::SettingsButton;
    const bool settingsPressed = pressed_ == Hit::SettingsButton;
    ui::DrawButton(graphics, fonts_, layout_.settings, i18n::Text(i18n::Str::Settings),
                   ui::ButtonStyle::Ghost, settingsHover, settingsPressed, true,
                   ui::Icon::Gear);
    ui::DrawButton(graphics, fonts_, layout_.more, L"", ui::ButtonStyle::Ghost,
                   hover_ == Hit::MoreButton, pressed_ == Hit::MoreButton, true,
                   ui::Icon::More);

    auto captionButton = [&](const Gdiplus::RectF& rect, ui::Icon icon, Hit id,
                             bool danger) {
        const bool hover = hover_ == id;
        if (hover || pressed_ == id) {
            const Gdiplus::Color fill = danger ? ui::colors::Danger() : ui::colors::HoverTint();
            ui::FillRoundRect(graphics, Inflate(rect, ui::ScaleF(-2.0f, dpi_),
                                                ui::ScaleF(-8.0f, dpi_)),
                              ui::ScaleF(6.0f, dpi_), fill);
        }
        const Gdiplus::Color color =
            danger && hover ? ui::colors::White() : ui::colors::TextSecondary();
        const float size = ui::ScaleF(14.0f, dpi_);
        ui::DrawIcon(graphics, icon,
                     Gdiplus::RectF(rect.X + (rect.Width - size) / 2.0f,
                                    rect.Y + (rect.Height - size) / 2.0f, size, size),
                     color, ui::ScaleF(1.3f, dpi_));
    };
    captionButton(layout_.minimize, ui::Icon::Minimize, Hit::MinimizeButton, false);
    captionButton(layout_.maximize,
                  IsZoomed(hwnd_) ? ui::Icon::Restore : ui::Icon::Maximize,
                  Hit::MaximizeButton, false);
    captionButton(layout_.close, ui::Icon::Close, Hit::CloseButton, true);
}

void MainWindow::DrawMainPage(Gdiplus::Graphics& graphics) {
    const bool enabled = lastSnapshot_.state == AppState::Ready;
    DrawTargetSection(graphics, enabled);
    DrawAudioSection(graphics, enabled);
    DrawOtherSection(graphics, enabled);
    DrawPreviewColumn(graphics, true);

    // Draggable divider: a quiet hairline at rest, a blue grip while hovered
    // or dragged. The whole 12 px gutter is the hit target.
    const Gdiplus::RectF splitter = layout_.splitter;
    if (splitter.Width > 0.0f && splitter.Height > 0.0f) {
        const bool hot = hover_ == Hit::Splitter || draggingSplitter_;
        const float centerX = splitter.X + splitter.Width / 2.0f;
        const float lineTop = splitter.Y + ui::ScaleF(8.0f, dpi_);
        const float lineBottom = splitter.GetBottom() - ui::ScaleF(8.0f, dpi_);
        if (hot) {
            ui::FillRoundRect(
                graphics,
                Gdiplus::RectF(centerX - ui::ScaleF(3.0f, dpi_), lineTop,
                               ui::ScaleF(6.0f, dpi_), lineBottom - lineTop),
                ui::ScaleF(3.0f, dpi_), ui::colors::PrimarySoft());
            ui::DrawLine(graphics, centerX, lineTop, centerX, lineBottom,
                         ui::colors::Primary(), ui::ScaleF(2.0f, dpi_));
        }
        // A small vertical grip stays visible at rest (and gives the gutter a
        // purpose besides whitespace); hover/drag turns it blue.
        const float centerY = (lineTop + lineBottom) / 2.0f;
        for (int i = -1; i <= 1; ++i) {
            const float dot = ui::ScaleF(3.0f, dpi_);
            ui::DrawCircle(
                graphics,
                Gdiplus::RectF(centerX - dot / 2.0f,
                               centerY + static_cast<float>(i) * ui::ScaleF(8.0f, dpi_) -
                                   dot / 2.0f,
                               dot, dot),
                hot ? ui::colors::Primary() : ui::colors::TextSecondary(),
                Gdiplus::Color(0, 0, 0, 0), 0.0f);
        }
    }
}

void MainWindow::DrawTargetSection(Gdiplus::Graphics& graphics, bool enabled) {
    const bool active = IsActiveState(lastSnapshot_.state) ||
                        lastSnapshot_.state == AppState::Stopping;
    const std::wstring title = i18n::Text(active ? i18n::Str::SectionTargetActive
                                                 : i18n::Str::SectionTarget);
    ui::DrawSectionHeader(graphics, fonts_, layout_.targetBadge, 1, title,
                          enabled ? ui::colors::Primary()
                                  : Gdiplus::Color(255, 195, 201, 212),
                          enabled ? ui::colors::Text() : ui::colors::TextTertiary());

    const Gdiplus::Color cardFill =
        enabled ? Gdiplus::Color(255, 249, 250, 253) : ui::colors::SurfaceMuted();
    const Gdiplus::Color border = enabled ? ui::colors::Border() : ui::colors::Divider();
    ui::FillRoundRectWithBorder(graphics, layout_.targetCard, ui::ScaleF(10.0f, dpi_),
                                cardFill, border);

    if (target_ != nullptr) {
        const Gdiplus::RectF iconRect = layout_.targetIcon;
        if (targetIcon_ && targetIcon_->GetLastStatus() == Gdiplus::Ok) {
            graphics.DrawImage(targetIcon_.get(), iconRect);
        } else {
            ui::FillRoundRect(graphics, iconRect, ui::ScaleF(8.0f, dpi_),
                              ui::colors::PrimarySoft());
            const float iconSize = ui::ScaleF(18.0f, dpi_);
            ui::DrawIcon(graphics, ui::Icon::Video,
                         Gdiplus::RectF(iconRect.X + (iconRect.Width - iconSize) / 2.0f,
                                        iconRect.Y + (iconRect.Height - iconSize) / 2.0f,
                                        iconSize, iconSize),
                         ui::colors::Primary(), ui::ScaleF(1.5f, dpi_));
        }
        const std::wstring primary =
            targetTitle_.empty() ? ExeDisplayName(targetExe_) : targetTitle_;
        ui::DrawText(graphics, fonts_, primary, layout_.targetName, ui::FontRole::BodyStrong,
                     enabled ? ui::colors::Text() : ui::colors::TextTertiary(),
                     ui::Align::Near, ui::VAlign::Center, true);
        ui::DrawText(graphics, fonts_, ExeDisplayName(targetExe_), layout_.targetSub,
                     ui::FontRole::Small, ui::colors::TextSecondary(), ui::Align::Near,
                     ui::VAlign::Center, true);
        ui::DrawButton(graphics, fonts_, layout_.targetChange,
                       i18n::Text(i18n::Str::ChangeWindow), ui::ButtonStyle::Secondary,
                       hover_ == Hit::TargetCard, pressed_ == Hit::TargetCard, enabled);
    } else {
        ui::FillRoundRect(graphics, layout_.targetIcon, ui::ScaleF(8.0f, dpi_),
                          ui::colors::PrimarySoft());
        const float iconSize = ui::ScaleF(18.0f, dpi_);
        ui::DrawIcon(graphics, ui::Icon::Window,
                     Gdiplus::RectF(layout_.targetIcon.X +
                                        (layout_.targetIcon.Width - iconSize) / 2.0f,
                                    layout_.targetIcon.Y +
                                        (layout_.targetIcon.Height - iconSize) / 2.0f,
                                    iconSize, iconSize),
                     ui::colors::Primary(), ui::ScaleF(1.5f, dpi_));
        ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::NoTarget), layout_.targetName,
                     ui::FontRole::BodyStrong, ui::colors::Text(), ui::Align::Near,
                     ui::VAlign::Center, false);
        const std::wstring hint = windows_.empty() ? i18n::Text(i18n::Str::NoWindows)
                                                   : i18n::Text(i18n::Str::ChooseWindow);
        ui::DrawText(graphics, fonts_, hint, layout_.targetSub, ui::FontRole::Small,
                     ui::colors::TextSecondary(), ui::Align::Near, ui::VAlign::Center,
                     true);
        ui::DrawButton(graphics, fonts_, layout_.targetChange,
                       i18n::Text(windows_.empty() ? i18n::Str::Rescan
                                                   : i18n::Str::ChooseWindow),
                       ui::ButtonStyle::Primary, hover_ == Hit::TargetCard,
                       pressed_ == Hit::TargetCard, enabled);
    }
}

void MainWindow::DrawAudioSection(Gdiplus::Graphics& graphics, bool enabled) {
    ui::DrawSectionHeader(graphics, fonts_, layout_.audioBadge, 2,
                          i18n::Text(i18n::Str::SectionAudio),
                          enabled ? ui::colors::Primary()
                                  : Gdiplus::Color(255, 195, 201, 212),
                          enabled ? ui::colors::Text() : ui::colors::TextTertiary());
    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::AudioHelp), layout_.audioHelp,
                 ui::FontRole::Small, enabled ? ui::colors::Primary()
                                              : ui::colors::TextTertiary(),
                 ui::Align::Far, ui::VAlign::Center, false);
    DrawAudioCard(graphics, 0, enabled);
    DrawAudioCard(graphics, 1, enabled);
}

void MainWindow::DrawAudioCard(Gdiplus::Graphics& graphics, int index, bool enabled) {
    const bool output = index == 0;
    const bool on = output ? systemAudio_ : microphone_;
    const bool hoverToggle = hover_ == (output ? Hit::SystemAudioToggle : Hit::MicToggle);
    const bool hoverDevice = hover_ == (output ? Hit::OutputDevice : Hit::InputDevice);

    const Gdiplus::RectF card = layout_.audioCard[index];
    ui::FillRoundRectWithBorder(graphics, card, ui::ScaleF(10.0f, dpi_),
                                enabled ? ui::colors::Surface()
                                        : ui::colors::SurfaceMuted(),
                                enabled ? ui::colors::Border() : ui::colors::Divider());

    const Gdiplus::Color iconColor =
        enabled ? ui::colors::TextSecondary() : ui::colors::TextTertiary();
    ui::DrawIcon(graphics, output ? ui::Icon::Speaker : ui::Icon::Microphone,
                 layout_.audioIcon[index], iconColor, ui::ScaleF(1.5f, dpi_));
    ui::DrawText(graphics, fonts_,
                 i18n::Text(output ? i18n::Str::RecordSystemAudio
                                   : i18n::Str::RecordMicrophone),
                 layout_.audioLabel[index], ui::FontRole::Body,
                 enabled ? ui::colors::Text() : ui::colors::TextTertiary(),
                 ui::Align::Near, ui::VAlign::Center, true);

    ui::DrawToggleSwitch(graphics, layout_.audioToggle[index], on, enabled, hoverToggle,
                         dpi_);

    const auto& devices = output ? outputDevices_ : inputDevices_;
    const std::wstring defaultName = output ? defaultOutputName_ : defaultInputName_;
    std::wstring deviceText;
    bool deviceAvailable = true;
    if (devices.empty() && defaultName.empty()) {
        deviceText = i18n::Text(i18n::Str::AudioNoDevices);
        deviceAvailable = false;
    } else {
        deviceText = AudioDeviceLabel(output);
    }
    const Gdiplus::RectF device = layout_.audioDevice[index];
    ui::FillRoundRectWithBorder(
        graphics, device, ui::ScaleF(6.0f, dpi_),
        enabled ? ui::colors::ControlBg() : ui::colors::SurfaceMuted(),
        !enabled ? ui::colors::Divider()
                 : (hoverDevice ? ui::colors::Primary() : ui::colors::Border()));
    ui::DrawText(graphics, fonts_, deviceText,
                 Gdiplus::RectF(device.X + ui::ScaleF(10.0f, dpi_), device.Y,
                                device.Width - ui::ScaleF(34.0f, dpi_), device.Height),
                 ui::FontRole::Small,
                 enabled && on && deviceAvailable ? ui::colors::Text()
                                                  : ui::colors::TextTertiary(),
                 ui::Align::Near, ui::VAlign::Center, true);
    const float chevron = ui::ScaleF(12.0f, dpi_);
    ui::DrawIcon(graphics, ui::Icon::ChevronDown,
                 Gdiplus::RectF(device.GetRight() - chevron - ui::ScaleF(10.0f, dpi_),
                                device.Y + (device.Height - chevron) / 2.0f, chevron,
                                chevron),
                 enabled && on ? ui::colors::TextSecondary()
                               : ui::colors::TextTertiary(),
                 ui::ScaleF(1.4f, dpi_));

    // The controls are disabled while recording, but the meters must keep
    // moving: they are how the user confirms both audio sources are alive.
    const float level = on ? (output ? systemLevel_ : micLevel_) : 0.0f;
    ui::DrawLevelMeter(graphics, layout_.audioMeter[index], level, 20, dpi_);
}

void MainWindow::DrawOtherSection(Gdiplus::Graphics& graphics, bool enabled) {
    ui::DrawSectionHeader(graphics, fonts_, layout_.otherBadge, 3,
                          i18n::Text(i18n::Str::SectionOther),
                          enabled ? ui::colors::Primary()
                                  : Gdiplus::Color(255, 195, 201, 212),
                          enabled ? ui::colors::Text() : ui::colors::TextTertiary());

    const Gdiplus::Color textColor =
        enabled ? ui::colors::Text() : ui::colors::TextTertiary();
    const Gdiplus::Color iconColor =
        enabled ? ui::colors::TextSecondary() : ui::colors::TextTertiary();

    ui::DrawIcon(graphics, ui::Icon::Sliders, layout_.qualityIcon, iconColor,
                 ui::ScaleF(1.5f, dpi_));
    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::Quality), layout_.qualityLabel,
                 ui::FontRole::Body, textColor, ui::Align::Near, ui::VAlign::Center,
                 false);
    ui::FillRoundRectWithBorder(graphics, layout_.qualityValue, ui::ScaleF(6.0f, dpi_),
                                enabled ? ui::colors::ControlBg()
                                        : ui::colors::SurfaceMuted(),
                                enabled ? ui::colors::Border() : ui::colors::Divider());
    ui::DrawText(graphics, fonts_, QualityLabel(),
                 Gdiplus::RectF(layout_.qualityValue.X + ui::ScaleF(10.0f, dpi_),
                                layout_.qualityValue.Y,
                                layout_.qualityValue.Width - ui::ScaleF(34.0f, dpi_),
                                layout_.qualityValue.Height),
                 ui::FontRole::Small, textColor, ui::Align::Near, ui::VAlign::Center,
                 true);
    const float chevron = ui::ScaleF(12.0f, dpi_);
    ui::DrawIcon(graphics, ui::Icon::ChevronDown,
                 Gdiplus::RectF(layout_.qualityValue.GetRight() - chevron -
                                    ui::ScaleF(10.0f, dpi_),
                                layout_.qualityValue.Y +
                                    (layout_.qualityValue.Height - chevron) / 2.0f,
                                chevron, chevron),
                 iconColor, ui::ScaleF(1.4f, dpi_));

    ui::DrawIcon(graphics, ui::Icon::Folder, layout_.saveIcon, iconColor,
                 ui::ScaleF(1.5f, dpi_));
    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::SaveLocation), layout_.saveLabel,
                 ui::FontRole::Body, textColor, ui::Align::Near, ui::VAlign::Center,
                 false);
    ui::FillRoundRectWithBorder(graphics, layout_.saveValue, ui::ScaleF(6.0f, dpi_),
                                enabled ? ui::colors::ControlBg()
                                        : ui::colors::SurfaceMuted(),
                                enabled ? ui::colors::Border() : ui::colors::Divider());
    ui::DrawText(graphics, fonts_,
                 ui::CompactPath(outputDirectory_, 64),
                 Gdiplus::RectF(layout_.saveValue.X + ui::ScaleF(10.0f, dpi_),
                                layout_.saveValue.Y,
                                layout_.saveValue.Width - ui::ScaleF(16.0f, dpi_),
                                layout_.saveValue.Height),
                 ui::FontRole::Small, textColor, ui::Align::Near, ui::VAlign::Center,
                 true);
    ui::DrawButton(graphics, fonts_, layout_.saveBrowse,
                   i18n::Text(i18n::Str::BrowseEllipsis), ui::ButtonStyle::Secondary,
                   hover_ == Hit::Browse, pressed_ == Hit::Browse, enabled);
}

void MainWindow::DrawPreviewColumn(Gdiplus::Graphics& graphics, bool enabled) {
    (void)enabled;
    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::Preview), layout_.previewTitle,
                 ui::FontRole::Section, ui::colors::Text(), ui::Align::Near,
                 ui::VAlign::Center, false);
    ui::DrawCheckBox(graphics, layout_.previewCheckBox, showPreview_,
                     lastSnapshot_.state != AppState::Stopping,
                     hover_ == Hit::PreviewToggle, dpi_);
    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::ShowPreview),
                 layout_.previewCheckLabel, ui::FontRole::Small,
                 lastSnapshot_.state == AppState::Stopping ? ui::colors::TextTertiary()
                                                           : ui::colors::TextSecondary(),
                 ui::Align::Near, ui::VAlign::Center, false);

    const Gdiplus::RectF card = layout_.previewCard;
    const float radius = ui::ScaleF(10.0f, dpi_);
    if (!showPreview_) {
        ui::FillRoundRectWithBorder(graphics, card, radius, ui::colors::SurfaceMuted(),
                                    ui::colors::Border());
        const float iconSize = ui::ScaleF(22.0f, dpi_);
        ui::DrawIcon(graphics, ui::Icon::Video,
                     Gdiplus::RectF(card.X + (card.Width - iconSize) / 2.0f,
                                    card.Y + card.Height / 2.0f - ui::ScaleF(18.0f, dpi_),
                                    iconSize, iconSize),
                     ui::colors::TextTertiary(), ui::ScaleF(1.5f, dpi_));
        ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::PreviewOff),
                     Gdiplus::RectF(card.X, card.Y + card.Height / 2.0f +
                                                ui::ScaleF(6.0f, dpi_),
                                    card.Width, ui::ScaleF(20.0f, dpi_)),
                     ui::FontRole::Small, ui::colors::TextTertiary(), ui::Align::Center,
                     ui::VAlign::Center, false);
    } else {
        ui::FillRoundRect(graphics, card, radius, Gdiplus::Color(255, 22, 26, 33));
        if (preview_ == nullptr || !preview_->HasFrame()) {
            const bool active = IsActiveState(lastSnapshot_.state);
            const std::wstring hint =
                target_ == nullptr
                    ? i18n::Text(i18n::Str::PreviewEmpty)
                    : (thumbnailFailed_
                           ? i18n::Text(i18n::Str::PreviewUnavailable)
                           : (active ? i18n::Text(i18n::Str::PreviewRecordingEmpty)
                                     : i18n::Text(i18n::Str::PreviewLoading)));
            const float iconSize = ui::ScaleF(24.0f, dpi_);
            ui::DrawIcon(graphics, ui::Icon::Window,
                         Gdiplus::RectF(card.X + (card.Width - iconSize) / 2.0f,
                                        card.Y + card.Height / 2.0f -
                                            ui::ScaleF(20.0f, dpi_),
                                        iconSize, iconSize),
                         Gdiplus::Color(170, 255, 255, 255), ui::ScaleF(1.5f, dpi_));
            ui::DrawTextWrapped(
                graphics, fonts_, hint,
                Gdiplus::RectF(card.X + ui::ScaleF(12.0f, dpi_),
                               card.Y + card.Height / 2.0f + ui::ScaleF(5.0f, dpi_),
                               card.Width - ui::ScaleF(24.0f, dpi_),
                               ui::ScaleF(40.0f, dpi_)),
                ui::FontRole::Small, Gdiplus::Color(190, 255, 255, 255),
                ui::Align::Center);
        }
        ui::StrokeRoundRect(graphics, card, radius, ui::colors::BorderStrong(), 1.0f);
    }

    ui::DrawTextWrapped(graphics, fonts_, i18n::Text(i18n::Str::PreviewHint),
                        layout_.previewCaption, ui::FontRole::Small,
                        ui::colors::TextSecondary(), ui::Align::Near);

    ui::FillRoundRectWithBorder(graphics, layout_.tipBox, ui::ScaleF(8.0f, dpi_),
                                ui::colors::InfoSoft(), ui::colors::InfoBorder());
    ui::DrawIcon(graphics, ui::Icon::Info, layout_.tipIcon, ui::colors::InfoText(),
                 ui::ScaleF(1.4f, dpi_));
    ui::DrawTextWrapped(
        graphics, fonts_, i18n::Text(i18n::Str::MinimizeHint),
        Gdiplus::RectF(layout_.tipBox.X + ui::ScaleF(34.0f, dpi_),
                       layout_.tipBox.Y + ui::ScaleF(7.0f, dpi_),
                       layout_.tipBox.Width - ui::ScaleF(44.0f, dpi_),
                       layout_.tipBox.Height - ui::ScaleF(12.0f, dpi_)),
        ui::FontRole::Small, ui::colors::InfoText(), ui::Align::Near);
}

void MainWindow::DrawFooter(Gdiplus::Graphics& graphics, const StatusSnapshot& snap) {
    ui::FillRect(graphics, layout_.footer, ui::colors::Surface());
    ui::DrawLine(graphics, layout_.footer.X, layout_.footer.Y + 0.5f,
                 layout_.footer.GetRight(), layout_.footer.Y + 0.5f,
                 ui::colors::Divider(), 1.0f);

    const Gdiplus::RectF icon = layout_.statusIcon;
    const float centerX = icon.X + icon.Width / 2.0f;
    const float centerY = icon.Y + icon.Height / 2.0f;
    std::wstring title;
    std::wstring sub;
    Gdiplus::Color titleColor = ui::colors::Text();
    bool showTimer = false;
    bool hasSub = false;

    switch (snap.state) {
        case AppState::Recording:
            title = i18n::Text(i18n::Str::Recording);
            titleColor = ui::colors::Danger();
            showTimer = true;
            ui::DrawCircle(graphics,
                           Gdiplus::RectF(centerX - ui::ScaleF(5.0f, dpi_),
                                          centerY - ui::ScaleF(5.0f, dpi_),
                                          ui::ScaleF(10.0f, dpi_),
                                          ui::ScaleF(10.0f, dpi_)),
                           ui::colors::Danger(), Gdiplus::Color(0, 0, 0, 0), 0.0f);
            break;
        case AppState::Paused: {
            title = i18n::Text(i18n::Str::Paused);
            titleColor = ui::colors::Warning();
            showTimer = true;
            hasSub = true;
            sub = i18n::Text(i18n::Str::PausedHint);
            ui::DrawCircle(graphics, icon, ui::colors::Warning(),
                           Gdiplus::Color(0, 0, 0, 0), 0.0f);
            const float pauseSize = ui::ScaleF(11.0f, dpi_);
            ui::DrawIcon(graphics, ui::Icon::Pause,
                         Gdiplus::RectF(centerX - pauseSize / 2.0f,
                                        centerY - pauseSize / 2.0f, pauseSize, pauseSize),
                         ui::colors::White(), ui::ScaleF(1.2f, dpi_));
            break;
        }
        case AppState::Preparing:
            title = i18n::Text(i18n::Str::Preparing);
            hasSub = true;
            sub = i18n::Text(i18n::Str::PreparingHint);
            DrawSpinner(graphics, icon, spinnerAngle_);
            break;
        case AppState::Stopping:
            title = i18n::Text(i18n::Str::Saving);
            hasSub = true;
            sub = i18n::Text(i18n::Str::SavingHint);
            DrawSpinner(graphics, icon, spinnerAngle_);
            break;
        case AppState::Faulted:
            title = i18n::ErrorTitle(snap.errorCode);
            titleColor = ui::colors::DangerText();
            hasSub = true;
            sub = i18n::Text(i18n::Str::StartFailedHint);
            ui::DrawCircle(graphics, icon, ui::colors::Danger(),
                           Gdiplus::Color(0, 0, 0, 0), 0.0f);
            break;
        default:
            title = i18n::Text(i18n::Str::Ready);
            titleColor = ui::colors::Text();
            hasSub = true;
            sub = target_ != nullptr ? i18n::Text(i18n::Str::ReadyHint)
                                     : i18n::Text(i18n::Str::ReadyNoTargetHint);
            ui::DrawCircle(graphics,
                           Gdiplus::RectF(centerX - ui::ScaleF(4.5f, dpi_),
                                          centerY - ui::ScaleF(4.5f, dpi_),
                                          ui::ScaleF(9.0f, dpi_),
                                          ui::ScaleF(9.0f, dpi_)),
                           ui::colors::Primary(), Gdiplus::Color(0, 0, 0, 0), 0.0f);
            break;
    }

    const float titleY = layout_.footer.Y + (hasSub ? ui::ScaleF(12.0f, dpi_)
                                                    : ui::ScaleF(20.0f, dpi_));
    Gdiplus::RectF titleRect(icon.GetRight() + ui::ScaleF(8.0f, dpi_), titleY,
                             ui::ScaleF(440.0f, dpi_), ui::ScaleF(24.0f, dpi_));
    if (showTimer) {
        const Gdiplus::SizeF titleSize =
            ui::MeasureText(graphics, fonts_, title, ui::FontRole::Title);
        titleRect.Width = titleSize.Width + ui::ScaleF(4.0f, dpi_);
    }
    ui::DrawText(graphics, fonts_, title, titleRect, ui::FontRole::Title, titleColor,
                 ui::Align::Near, ui::VAlign::Center, false);
    if (showTimer) {
        ui::DrawText(graphics, fonts_, ElapsedText(),
                     Gdiplus::RectF(titleRect.GetRight() + ui::ScaleF(12.0f, dpi_), titleY,
                                    ui::ScaleF(160.0f, dpi_), ui::ScaleF(24.0f, dpi_)),
                     ui::FontRole::Timer, titleColor, ui::Align::Near, ui::VAlign::Center,
                     false);
    }
    if (hasSub) {
        ui::DrawText(graphics, fonts_, sub,
                     Gdiplus::RectF(icon.GetRight() + ui::ScaleF(8.0f, dpi_),
                                    layout_.footer.Y + ui::ScaleF(34.0f, dpi_),
                                    ui::ScaleF(520.0f, dpi_),
                                    ui::ScaleF(18.0f, dpi_)),
                     ui::FontRole::Small, ui::colors::TextSecondary(), ui::Align::Near,
                     ui::VAlign::Center, true);
    }

    if (snap.state == AppState::Ready) {
        ui::DrawButton(graphics, fonts_, layout_.primaryButton,
                       i18n::Text(i18n::Str::StartRecording), ui::ButtonStyle::Primary,
                       hover_ == Hit::Start, pressed_ == Hit::Start,
                       target_ != nullptr, ui::Icon::Record);
    } else if (snap.state == AppState::Recording) {
        ui::DrawButton(graphics, fonts_, layout_.secondaryButton,
                       i18n::Text(i18n::Str::Pause), ui::ButtonStyle::Secondary,
                       hover_ == Hit::PauseResume, pressed_ == Hit::PauseResume, true,
                       ui::Icon::Pause);
        ui::DrawButton(graphics, fonts_, layout_.stopButton,
                       i18n::Text(i18n::Str::StopAndSave), ui::ButtonStyle::Danger,
                       hover_ == Hit::Stop, pressed_ == Hit::Stop, true,
                       ui::Icon::Square);
    } else if (snap.state == AppState::Paused) {
        ui::DrawButton(graphics, fonts_, layout_.primaryButton,
                       i18n::Text(i18n::Str::Resume), ui::ButtonStyle::Primary,
                       hover_ == Hit::PauseResume, pressed_ == Hit::PauseResume, true,
                       ui::Icon::Play);
        ui::DrawButton(graphics, fonts_, layout_.stopButton,
                       i18n::Text(i18n::Str::StopAndSave), ui::ButtonStyle::Secondary,
                       hover_ == Hit::Stop, pressed_ == Hit::Stop, true,
                       ui::Icon::Square);
    }
}

void MainWindow::DrawSpinner(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect,
                             float angle) {
    Gdiplus::Pen base(ui::colors::Divider(), ui::ScaleF(2.0f, dpi_));
    graphics.DrawArc(&base, rect.X, rect.Y, rect.Width, rect.Height, 0.0f, 360.0f);
    Gdiplus::Pen active(ui::colors::Primary(), ui::ScaleF(2.2f, dpi_));
    active.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound,
                      Gdiplus::DashCapRound);
    graphics.DrawArc(&active, rect.X, rect.Y, rect.Width, rect.Height, angle, 260.0f);
}

void MainWindow::DrawResultPage(Gdiplus::Graphics& graphics) {
    ui::FillRoundRectWithBorder(graphics, layout_.resultArea, ui::ScaleF(12.0f, dpi_),
                                ui::colors::Surface(), ui::colors::Border());

    const bool warning = resultHadError_ || resultTargetLost_;
    const Gdiplus::RectF icon = layout_.resultIcon;
    if (warning) {
        ui::DrawIcon(graphics, ui::Icon::Alert, icon, ui::colors::Warning(),
                     ui::ScaleF(2.0f, dpi_));
    } else {
        ui::DrawCircle(graphics, icon, ui::colors::Success(), Gdiplus::Color(0, 0, 0, 0),
                       0.0f);
        const float check = ui::ScaleF(30.0f, dpi_);
        ui::DrawIcon(graphics, ui::Icon::Check,
                     Gdiplus::RectF(icon.X + (icon.Width - check) / 2.0f,
                                    icon.Y + (icon.Height - check) / 2.0f, check, check),
                     ui::colors::White(), ui::ScaleF(2.4f, dpi_));
    }

    std::wstring title;
    std::wstring sub;
    Gdiplus::Color subColor = ui::colors::TextSecondary();
    if (resultHadError_) {
        title = i18n::Text(i18n::Str::ErrorSavedTitle);
        sub = resultErrorCode_ != ErrorCode::None
                  ? i18n::ErrorMessage(resultErrorCode_)
                  : i18n::Text(i18n::Str::EmptyRecordingHint);
        subColor = ui::colors::DangerText();
    } else if (resultTargetLost_) {
        title = i18n::Text(i18n::Str::ErrTargetLostTitle);
        sub = i18n::Text(i18n::Str::ErrTargetLostBody);
    } else {
        title = i18n::Text(i18n::Str::RecordingComplete);
        sub = i18n::Text(i18n::Str::ResultSavedHint);
    }
    ui::DrawText(graphics, fonts_, title, layout_.resultTitle, ui::FontRole::Big,
                 ui::colors::Text(), ui::Align::Near, ui::VAlign::Center, true);
    ui::DrawTextWrapped(graphics, fonts_, sub, layout_.resultSub, ui::FontRole::Small,
                        subColor, ui::Align::Near);

    const Gdiplus::RectF thumb = layout_.resultThumb;
    ui::FillRoundRect(graphics, thumb, ui::ScaleF(8.0f, dpi_),
                      Gdiplus::Color(255, 22, 26, 33));
    if (!resultThumb_.empty() && resultThumbWidth_ > 0 && resultThumbHeight_ > 0) {
        const INT stride = static_cast<INT>(resultThumbWidth_) * 4;
        Gdiplus::Bitmap bitmap(static_cast<INT>(resultThumbWidth_),
                               static_cast<INT>(resultThumbHeight_), stride,
                               PixelFormat32bppRGB, resultThumb_.data());
        if (bitmap.GetLastStatus() == Gdiplus::Ok) {
            const float scale = std::min(thumb.Width / resultThumbWidth_,
                                         thumb.Height / resultThumbHeight_);
            const float drawW = resultThumbWidth_ * scale;
            const float drawH = resultThumbHeight_ * scale;
            Gdiplus::GraphicsState state = graphics.Save();
            Gdiplus::GraphicsPath clip;
            const float radius = ui::ScaleF(8.0f, dpi_);
            clip.AddArc(thumb.X, thumb.Y, radius * 2, radius * 2, 180.0f, 90.0f);
            clip.AddArc(thumb.GetRight() - radius * 2, thumb.Y, radius * 2, radius * 2,
                        270.0f, 90.0f);
            clip.AddArc(thumb.GetRight() - radius * 2, thumb.GetBottom() - radius * 2,
                        radius * 2, radius * 2, 0.0f, 90.0f);
            clip.AddArc(thumb.X, thumb.GetBottom() - radius * 2, radius * 2, radius * 2,
                        90.0f, 90.0f);
            clip.CloseFigure();
            graphics.SetClip(&clip);
            graphics.DrawImage(&bitmap,
                               Gdiplus::RectF(thumb.X + (thumb.Width - drawW) / 2.0f,
                                              thumb.Y + (thumb.Height - drawH) / 2.0f,
                                              drawW, drawH));
            graphics.Restore(state);
        }
    } else {
        const float iconSize = ui::ScaleF(26.0f, dpi_);
        ui::DrawIcon(graphics, ui::Icon::Video,
                     Gdiplus::RectF(thumb.X + (thumb.Width - iconSize) / 2.0f,
                                    thumb.Y + (thumb.Height - iconSize) / 2.0f - 8.0f,
                                    iconSize, iconSize),
                     Gdiplus::Color(150, 255, 255, 255), ui::ScaleF(1.5f, dpi_));
    }
    const float playSize = ui::ScaleF(38.0f, dpi_);
    const Gdiplus::RectF play(layout_.resultPlay.X, layout_.resultPlay.Y, playSize,
                              playSize);
    ui::DrawCircle(graphics, play, Gdiplus::Color(150, 15, 18, 24),
                   Gdiplus::Color(120, 255, 255, 255), 1.0f);
    const float triangle = ui::ScaleF(15.0f, dpi_);
    ui::DrawIcon(graphics, ui::Icon::Play,
                 Gdiplus::RectF(play.X + (play.Width - triangle) / 2.0f +
                                    ui::ScaleF(1.0f, dpi_),
                                play.Y + (play.Height - triangle) / 2.0f, triangle,
                                triangle),
                 ui::colors::White(), ui::ScaleF(1.0f, dpi_));
    {
        const std::wstring duration = i18n::FormatDuration(resultDurationMs_);
        const Gdiplus::SizeF size =
            ui::MeasureText(graphics, fonts_, duration, ui::FontRole::Small);
        const Gdiplus::RectF badge(thumb.GetRight() - size.Width -
                                       ui::ScaleF(20.0f, dpi_),
                                   thumb.GetBottom() - ui::ScaleF(24.0f, dpi_),
                                   size.Width + ui::ScaleF(12.0f, dpi_),
                                   ui::ScaleF(18.0f, dpi_));
        ui::FillRoundRect(graphics, badge, ui::ScaleF(4.0f, dpi_),
                          Gdiplus::Color(170, 10, 12, 16));
        ui::DrawText(graphics, fonts_, duration, badge, ui::FontRole::Small,
                     ui::colors::White(), ui::Align::Center, ui::VAlign::Center, false);
    }

    struct Row {
        ui::Icon icon;
        std::wstring label;
        std::wstring value;
        Gdiplus::Color valueColor;
    };
    const std::wstring parent = ParentFolder(resultPath_);
    const Row rows[4] = {
        {ui::Icon::File, i18n::Text(i18n::Str::FileName), resultName_,
         ui::colors::Text()},
        {ui::Icon::Clock, i18n::Text(i18n::Str::Duration),
         i18n::FormatDuration(resultDurationMs_), ui::colors::Text()},
        {ui::Icon::File, i18n::Text(i18n::Str::FileSize),
         i18n::FormatBytes(resultBytes_), ui::colors::Text()},
        {ui::Icon::Folder, i18n::Text(i18n::Str::SavedIn),
         ui::CompactPath(parent, 58), ui::colors::Primary()}};
    float rowY = layout_.resultDetails.Y;
    const float rowHeight = ui::ScaleF(26.0f, dpi_);
    for (const Row& row : rows) {
        const float iconSize = ui::ScaleF(14.0f, dpi_);
        ui::DrawIcon(graphics, row.icon,
                     Gdiplus::RectF(layout_.resultDetails.X,
                                    rowY + (rowHeight - iconSize) / 2.0f, iconSize,
                                    iconSize),
                     ui::colors::TextTertiary(), ui::ScaleF(1.4f, dpi_));
        ui::DrawText(graphics, fonts_, row.label,
                     Gdiplus::RectF(layout_.resultDetails.X + ui::ScaleF(22.0f, dpi_),
                                    rowY, ui::ScaleF(58.0f, dpi_), rowHeight),
                     ui::FontRole::Small, ui::colors::TextSecondary(), ui::Align::Near,
                     ui::VAlign::Center, false);
        ui::DrawText(graphics, fonts_, row.value,
                     Gdiplus::RectF(layout_.resultDetails.X + ui::ScaleF(84.0f, dpi_),
                                    rowY,
                                    layout_.resultDetails.Width -
                                        ui::ScaleF(84.0f, dpi_),
                                    rowHeight),
                     ui::FontRole::Small, row.valueColor, ui::Align::Near,
                     ui::VAlign::Center, true);
        rowY += rowHeight;
    }

    ui::DrawButton(graphics, fonts_, layout_.resultPrimary,
                   i18n::Text(i18n::Str::OpenFile), ui::ButtonStyle::Primary,
                   hover_ == Hit::ResultOpenFile, pressed_ == Hit::ResultOpenFile, true,
                   ui::Icon::External);
    ui::DrawButton(graphics, fonts_, layout_.resultSecondary,
                   i18n::Text(i18n::Str::OpenFolder), ui::ButtonStyle::Secondary,
                   hover_ == Hit::ResultOpenFolder, pressed_ == Hit::ResultOpenFolder,
                   true, ui::Icon::Folder);
    ui::DrawButton(graphics, fonts_, layout_.resultTertiary,
                   i18n::Text(i18n::Str::RecordAgain), ui::ButtonStyle::Secondary,
                   hover_ == Hit::ResultAgain, pressed_ == Hit::ResultAgain, true,
                   ui::Icon::Refresh);
}

void MainWindow::DrawErrorPage(Gdiplus::Graphics& graphics) {
    ui::FillRoundRectWithBorder(graphics, layout_.errorArea, ui::ScaleF(12.0f, dpi_),
                                ui::colors::Surface(), ui::colors::Border());
    ui::DrawIcon(graphics, ui::Icon::Alert, layout_.errorIcon, ui::colors::Danger(),
                 ui::ScaleF(2.0f, dpi_));
    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::RecordingFailed),
                 layout_.errorTitle, ui::FontRole::Big, ui::colors::Text(),
                 ui::Align::Near, ui::VAlign::Center, true);
    ui::DrawTextWrapped(graphics, fonts_, i18n::Text(i18n::Str::StartFailedHint),
                        layout_.errorSub, ui::FontRole::Small,
                        ui::colors::TextSecondary(), ui::Align::Near);

    const Gdiplus::RectF box = layout_.errorBox;
    ui::FillRoundRectWithBorder(graphics, box, ui::ScaleF(8.0f, dpi_),
                                ui::colors::DangerSoft(), ui::colors::DangerBorder());
    std::wstring codeLine = std::wstring(i18n::Text(i18n::Str::ErrorCodeLabel)) + L": ";
    if (errorCode_ != ErrorCode::None) {
        codeLine += Utf8ToWide(vsr::ToString(errorCode_));
    } else {
        codeLine += L"-";
    }
    ui::DrawText(graphics, fonts_, codeLine,
                 Gdiplus::RectF(box.X + ui::ScaleF(14.0f, dpi_),
                                box.Y + ui::ScaleF(8.0f, dpi_), box.Width -
                                    ui::ScaleF(28.0f, dpi_),
                                ui::ScaleF(20.0f, dpi_)),
                 ui::FontRole::BodyStrong, ui::colors::DangerText(), ui::Align::Near,
                 ui::VAlign::Center, true);
    const std::wstring detail =
        errorCode_ != ErrorCode::None ? i18n::ErrorMessage(errorCode_)
                                      : i18n::Text(i18n::Str::StartFailedHint);
    ui::DrawTextWrapped(
        graphics, fonts_, std::wstring(i18n::Text(i18n::Str::ErrorDetailLabel)) + L": " +
                              detail,
        Gdiplus::RectF(box.X + ui::ScaleF(14.0f, dpi_),
                       box.Y + ui::ScaleF(30.0f, dpi_),
                       box.Width - ui::ScaleF(28.0f, dpi_),
                       box.Height - ui::ScaleF(36.0f, dpi_)),
        ui::FontRole::Small, ui::colors::DangerText(), ui::Align::Near);

    ui::DrawButton(graphics, fonts_, layout_.errorRetry, i18n::Text(i18n::Str::Retry),
                   ui::ButtonStyle::Primary, hover_ == Hit::ErrorRetry,
                   pressed_ == Hit::ErrorRetry, true, ui::Icon::Refresh);
    ui::DrawButton(graphics, fonts_, layout_.errorHelp, i18n::Text(i18n::Str::ViewHelp),
                   ui::ButtonStyle::Secondary, hover_ == Hit::ErrorHelp,
                   pressed_ == Hit::ErrorHelp, true, ui::Icon::Help);
    ui::DrawButton(graphics, fonts_, layout_.errorDiag,
                   i18n::Text(i18n::Str::CreateDiagnostics), ui::ButtonStyle::Secondary,
                   hover_ == Hit::ErrorDiagnostics, pressed_ == Hit::ErrorDiagnostics,
                   true, ui::Icon::Diagnostics);
}

void MainWindow::UpdateLayout() {
    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    const float width = static_cast<float>(rc.right - rc.left);
    const float height = static_cast<float>(rc.bottom - rc.top);
    if (width <= 0.0f || height <= 0.0f) return;

    const auto S = [&](int value) { return ui::ScaleF(static_cast<float>(value), dpi_); };
    Layout layout;
    layout.titleBar = Gdiplus::RectF(0.0f, 0.0f, width, S(44));
    layout.logo = Gdiplus::RectF(S(16), S(9), S(26), S(26));
    layout.appTitle = Gdiplus::RectF(S(50), S(5), S(240), S(22));
    layout.tagline = Gdiplus::RectF(S(50), S(23), S(300), S(18));
    layout.close = Gdiplus::RectF(width - S(44), 0.0f, S(44), S(44));
    layout.maximize = Gdiplus::RectF(width - S(88), 0.0f, S(44), S(44));
    layout.minimize = Gdiplus::RectF(width - S(132), 0.0f, S(44), S(44));
    layout.more = Gdiplus::RectF(width - S(172), S(5), S(36), S(34));
    layout.settings = Gdiplus::RectF(width - S(252), S(5), S(76), S(34));

    const float pad = S(16);
    const float contentTop = S(44) + S(10);
    const float footerTop = height - S(68);
    const float contentBottom = footerTop - S(6);

    if (page_ == Page::Main) {
        const float gap = S(12);
        const float available = width - pad * 2.0f - gap;
        const float minLeft = S(380);
        const float minRight = S(260);
        float rightWidth = available * previewRatio_;
        rightWidth = std::clamp(rightWidth, minRight,
                                std::max(minRight, available - minLeft));
        const float leftX = pad;
        const float leftWidth = available - rightWidth;
        const float rightX = leftX + leftWidth + gap;
        layout.splitter =
            Gdiplus::RectF(leftX + leftWidth, contentTop, gap, contentBottom - contentTop);
        layoutAvailableWidth_ = available;
        layoutRightWidth_ = rightWidth;

        float y = contentTop;
        layout.targetBadge = Gdiplus::RectF(leftX, y + S(1), S(18), S(18));
        layout.targetTitle = Gdiplus::RectF(leftX + S(28), y, leftWidth - S(28), S(18));
        y += S(24);
        layout.targetCard = Gdiplus::RectF(leftX, y, leftWidth, S(54));
        layout.targetIcon = Gdiplus::RectF(leftX + S(12), y + S(11), S(32), S(32));
        layout.targetChange =
            Gdiplus::RectF(leftX + leftWidth - S(124), y + S(12), S(112), S(30));
        layout.targetName = Gdiplus::RectF(leftX + S(56), y + S(6),
                                           leftWidth - S(56) - S(136), S(22));
        layout.targetSub = Gdiplus::RectF(leftX + S(56), y + S(28),
                                          leftWidth - S(56) - S(136), S(18));
        y += S(54) + S(8);

        layout.audioBadge = Gdiplus::RectF(leftX, y + S(1), S(18), S(18));
        layout.audioTitle = Gdiplus::RectF(leftX + S(28), y, leftWidth - S(120), S(18));
        layout.audioHelp = Gdiplus::RectF(leftX + leftWidth - S(92), y, S(92), S(18));
        y += S(24);
        for (int i = 0; i < 2; ++i) {
            layout.audioCard[i] = Gdiplus::RectF(leftX, y, leftWidth, S(74));
            layout.audioIcon[i] =
                Gdiplus::RectF(leftX + S(14), y + S(10), S(18), S(18));
            layout.audioLabel[i] = Gdiplus::RectF(leftX + S(42), y + S(8),
                                                  leftWidth - S(42) - S(74), S(24));
            layout.audioToggle[i] = Gdiplus::RectF(leftX + leftWidth - S(14) - S(42),
                                                   y + S(8), S(42), S(24));
            layout.audioDevice[i] = Gdiplus::RectF(leftX + S(12), y + S(34),
                                                   leftWidth - S(24), S(27));
            layout.audioMeter[i] =
                Gdiplus::RectF(leftX + S(14), y + S(64), S(180), S(5));
            y += S(74) + (i == 0 ? S(6) : S(8));
        }

        layout.otherBadge = Gdiplus::RectF(leftX, y + S(1), S(18), S(18));
        layout.otherTitle = Gdiplus::RectF(leftX + S(28), y, leftWidth - S(28), S(18));
        y += S(24);
        layout.qualityIcon = Gdiplus::RectF(leftX + S(2), y + S(8), S(18), S(18));
        layout.qualityLabel =
            Gdiplus::RectF(leftX + S(28), y, S(58), S(34));
        layout.qualityValue =
            Gdiplus::RectF(leftX + S(88), y, leftWidth - S(88), S(34));
        y += S(40);
        layout.saveIcon = Gdiplus::RectF(leftX + S(2), y + S(8), S(18), S(18));
        layout.saveLabel = Gdiplus::RectF(leftX + S(28), y, S(58), S(34));
        layout.saveBrowse =
            Gdiplus::RectF(leftX + leftWidth - S(70), y + S(1), S(70), S(32));
        layout.saveValue = Gdiplus::RectF(leftX + S(88), y + S(1),
                                          leftWidth - S(88) - S(78), S(32));

        layout.previewTitle = Gdiplus::RectF(rightX, contentTop, S(160), S(18));
        layout.previewCheckBox =
            Gdiplus::RectF(rightX + rightWidth - S(104), contentTop + S(1), S(17), S(17));
        layout.previewCheckLabel =
            Gdiplus::RectF(rightX + rightWidth - S(82), contentTop, S(82), S(18));
        const float previewY = contentTop + S(24);
        const float previewH =
            std::min(rightWidth * 9.0f / 16.0f,
                     contentBottom - previewY - S(24) - S(38) - S(8) - S(52));
        layout.previewCard =
            Gdiplus::RectF(rightX, previewY, rightWidth, std::max(S(120), previewH));
        layout.previewCaption =
            Gdiplus::RectF(rightX, layout.previewCard.GetBottom() + S(8), rightWidth,
                           S(32));
        const float tipY = layout.previewCaption.GetBottom() + S(8);
        layout.tipBox =
            Gdiplus::RectF(rightX, tipY, rightWidth,
                           std::min(S(62), std::max(S(48), contentBottom - tipY)));
        layout.tipIcon = Gdiplus::RectF(layout.tipBox.X + S(11),
                                        layout.tipBox.Y + S(12), S(16), S(16));

        layout.footer = Gdiplus::RectF(0, footerTop, width, S(68));
        layout.statusIcon = Gdiplus::RectF(pad + S(2), footerTop + S(22), S(24), S(24));
        layout.statusTitle =
            Gdiplus::RectF(pad + S(34), footerTop + S(12), S(420), S(24));
        layout.statusSub =
            Gdiplus::RectF(pad + S(34), footerTop + S(34), S(480), S(18));
        const float buttonY = footerTop + (S(68) - S(40)) / 2.0f;
        const float stopX = width - pad - S(160);
        if (lastSnapshot_.state == AppState::Recording) {
            layout.secondaryButton =
                Gdiplus::RectF(stopX - S(10) - S(128), buttonY, S(128), S(40));
            layout.stopButton = Gdiplus::RectF(stopX, buttonY, S(160), S(40));
        } else if (lastSnapshot_.state == AppState::Paused) {
            layout.primaryButton =
                Gdiplus::RectF(stopX - S(10) - S(144), buttonY, S(144), S(40));
            layout.stopButton = Gdiplus::RectF(stopX, buttonY, S(160), S(40));
        } else {
            layout.primaryButton =
                Gdiplus::RectF(width - pad - S(176), buttonY, S(176), S(40));
        }
    } else if (page_ == Page::Result) {
        const float areaX = pad;
        const float areaY = contentTop;
        const float areaW = width - pad * 2.0f;
        const float areaH = height - areaY - S(8);
        layout.resultArea = Gdiplus::RectF(areaX, areaY, areaW, areaH);
        layout.resultIcon = Gdiplus::RectF(areaX + S(22), areaY + S(18), S(48), S(48));
        layout.resultTitle =
            Gdiplus::RectF(layout.resultIcon.GetRight() + S(16), areaY + S(16),
                           areaW - S(240), S(28));
        layout.resultSub =
            Gdiplus::RectF(layout.resultIcon.GetRight() + S(16), areaY + S(44),
                           areaW - S(240), S(34));
        const float thumbW = std::min(S(200), width * 0.28f);
        const float thumbH = thumbW * 9.0f / 16.0f;
        const float thumbY = layout.resultIcon.GetBottom() + S(16);
        layout.resultThumb = Gdiplus::RectF(areaX + S(22), thumbY, thumbW, thumbH);
        layout.resultPlay = Gdiplus::RectF(layout.resultThumb.X +
                                               layout.resultThumb.Width / 2.0f -
                                               S(19),
                                           layout.resultThumb.Y +
                                               layout.resultThumb.Height / 2.0f -
                                               S(19),
                                           S(38), S(38));
        layout.resultPrimary =
            Gdiplus::RectF(areaX + areaW - S(150), areaY + S(16), S(150), S(40));
        layout.resultSecondary =
            Gdiplus::RectF(areaX + areaW - S(150), areaY + S(64), S(150), S(40));
        layout.resultTertiary =
            Gdiplus::RectF(areaX + areaW - S(150), areaY + S(112), S(150), S(40));
        layout.resultDetails =
            Gdiplus::RectF(layout.resultThumb.GetRight() + S(24), thumbY,
                           layout.resultPrimary.X -
                               (layout.resultThumb.GetRight() + S(24)) - S(16),
                           thumbH);
        layout.footer = Gdiplus::RectF(0, height - S(68), width, S(68));
    } else {
        const float areaX = pad;
        const float areaY = contentTop;
        const float areaW = width - pad * 2.0f;
        const float areaH = height - areaY - S(8);
        layout.errorArea = Gdiplus::RectF(areaX, areaY, areaW, areaH);
        layout.errorIcon = Gdiplus::RectF(areaX + S(22), areaY + S(18), S(44), S(44));
        layout.errorTitle =
            Gdiplus::RectF(layout.errorIcon.GetRight() + S(16), areaY + S(16),
                           areaW - S(240), S(30));
        layout.errorSub =
            Gdiplus::RectF(layout.errorIcon.GetRight() + S(16), areaY + S(46),
                           areaW - S(120), S(44));
        layout.errorBox =
            Gdiplus::RectF(areaX + S(22), areaY + S(106), areaW - S(44), S(76));
        const float buttonY = areaY + areaH - S(48);
        layout.errorRetry =
            Gdiplus::RectF(areaX + S(22), buttonY, S(140), S(40));
        layout.errorHelp =
            Gdiplus::RectF(layout.errorRetry.GetRight() + S(10), buttonY, S(140), S(40));
        layout.errorDiag =
            Gdiplus::RectF(layout.errorHelp.GetRight() + S(10), buttonY, S(180), S(40));
        layout.footer = Gdiplus::RectF(0, height - S(68), width, S(68));
    }

    layout_ = layout;
    BuildHitTargets();
    UpdatePreviewChild();
}

void MainWindow::UpdatePreviewChild() {
    if (preview_ == nullptr || preview_->Hwnd() == nullptr) return;
    const bool visible = page_ == Page::Main && showPreview_ && preview_->HasFrame() &&
                         !IsIconic(hwnd_);
    if (!visible) {
        if (previewChildShown_) {
            ShowWindow(preview_->Hwnd(), SW_HIDE);
            previewChildShown_ = false;
        }
        return;
    }
    const Gdiplus::RectF card = layout_.previewCard;
    const int x = static_cast<int>(card.X) + 1;
    const int y = static_cast<int>(card.Y) + 1;
    const int w = std::max(1, static_cast<int>(card.Width) - 2);
    const int h = std::max(1, static_cast<int>(card.Height) - 2);
    SetWindowPos(preview_->Hwnd(), HWND_TOP, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    HRGN region = CreateRoundRectRgn(0, 0, w + 1, h + 1, ui::Scale(10, dpi_),
                                     ui::Scale(10, dpi_));
    SetWindowRgn(preview_->Hwnd(), region, TRUE);
    preview_->UpdateThumbnailRect();
    previewChildShown_ = true;
}

void MainWindow::BuildHitTargets() {
    hits_.clear();
    auto add = [&](Hit id, const Gdiplus::RectF& rect) {
        if (IsHitEnabled(id)) hits_.push_back(HitTarget{id, rect});
    };

    if (page_ == Page::Main) {
        add(Hit::TargetCard, layout_.targetCard);
        add(Hit::AudioHelp, layout_.audioHelp);
        add(Hit::SystemAudioToggle, layout_.audioToggle[0]);
        add(Hit::OutputDevice, layout_.audioDevice[0]);
        add(Hit::MicToggle, layout_.audioToggle[1]);
        add(Hit::InputDevice, layout_.audioDevice[1]);
        add(Hit::Quality, layout_.qualityValue);
        add(Hit::SavePath, layout_.saveValue);
        add(Hit::Browse, layout_.saveBrowse);
        add(Hit::PreviewToggle,
            Union(layout_.previewCheckBox, layout_.previewCheckLabel));
        add(Hit::Splitter, layout_.splitter);
        if (lastSnapshot_.state == AppState::Recording) {
            add(Hit::PauseResume, layout_.secondaryButton);
            add(Hit::Stop, layout_.stopButton);
        } else if (lastSnapshot_.state == AppState::Paused) {
            add(Hit::PauseResume, layout_.primaryButton);
            add(Hit::Stop, layout_.stopButton);
        } else {
            add(Hit::Start, layout_.primaryButton);
        }
    } else if (page_ == Page::Result) {
        add(Hit::ResultOpenFile, layout_.resultPrimary);
        add(Hit::ResultOpenFolder, layout_.resultSecondary);
        add(Hit::ResultAgain, layout_.resultTertiary);
    } else {
        add(Hit::ErrorRetry, layout_.errorRetry);
        add(Hit::ErrorHelp, layout_.errorHelp);
        add(Hit::ErrorDiagnostics, layout_.errorDiag);
    }

    add(Hit::SettingsButton, layout_.settings);
    add(Hit::MoreButton, layout_.more);
    add(Hit::MinimizeButton, layout_.minimize);
    add(Hit::MaximizeButton, layout_.maximize);
    add(Hit::CloseButton, layout_.close);

    auto stillValid = [&](Hit id) {
        return std::any_of(hits_.begin(), hits_.end(),
                           [id](const HitTarget& hit) { return hit.id == id; });
    };
    if (hover_ != Hit::None && !stillValid(hover_)) hover_ = Hit::None;
    if (pressed_ != Hit::None && !stillValid(pressed_)) pressed_ = Hit::None;
    if (focus_ != Hit::None && !stillValid(focus_)) focus_ = Hit::None;
}

bool MainWindow::IsHitEnabled(Hit id) const {
    const AppState state = lastSnapshot_.state;
    switch (id) {
        case Hit::SettingsButton:
        case Hit::MoreButton:
        case Hit::MinimizeButton:
        case Hit::MaximizeButton:
        case Hit::CloseButton:
            return true;
        case Hit::TargetCard:
        case Hit::AudioHelp:
        case Hit::SystemAudioToggle:
        case Hit::MicToggle:
        case Hit::Quality:
        case Hit::SavePath:
        case Hit::Browse:
            return page_ == Page::Main && state == AppState::Ready;
        case Hit::OutputDevice:
            return page_ == Page::Main && state == AppState::Ready &&
                   !outputDevices_.empty();
        case Hit::InputDevice:
            return page_ == Page::Main && state == AppState::Ready &&
                   !inputDevices_.empty();
        case Hit::PreviewToggle:
            return page_ == Page::Main && state != AppState::Stopping;
        case Hit::Splitter:
            return page_ == Page::Main;
        case Hit::Start:
            return page_ == Page::Main && state == AppState::Ready && target_ != nullptr;
        case Hit::PauseResume:
            return page_ == Page::Main &&
                   (state == AppState::Recording || state == AppState::Paused);
        case Hit::Stop:
            return page_ == Page::Main &&
                   (state == AppState::Recording || state == AppState::Paused);
        case Hit::ResultOpenFile:
        case Hit::ResultOpenFolder:
        case Hit::ResultAgain:
            return page_ == Page::Result;
        case Hit::ErrorRetry:
        case Hit::ErrorHelp:
        case Hit::ErrorDiagnostics:
            return page_ == Page::Error;
        default:
            return false;
    }
}

MainWindow::Hit MainWindow::HitTest(POINT clientPoint) const {
    for (const HitTarget& hit : hits_) {
        if (Contains(hit.rect, clientPoint)) return hit.id;
    }
    return Hit::None;
}

void MainWindow::SetHover(Hit hit) {
    if (hover_ == hit) return;
    hover_ = hit;
    InvalidateAll();
}

void MainWindow::SetFocusHit(Hit hit, bool keyboard) {
    focus_ = hit;
    keyboardFocus_ = keyboard;
    if (hit != Hit::None) SetFocus(hwnd_);
    InvalidateAll();
}

void MainWindow::CycleFocus(bool backwards) {
    if (hits_.empty()) return;
    int index = -1;
    for (size_t i = 0; i < hits_.size(); ++i) {
        if (hits_[i].id == focus_) {
            index = static_cast<int>(i);
            break;
        }
    }
    if (index < 0) {
        index = backwards ? static_cast<int>(hits_.size()) - 1 : 0;
    } else {
        index += backwards ? -1 : 1;
        if (index < 0) index = static_cast<int>(hits_.size()) - 1;
        if (index >= static_cast<int>(hits_.size())) index = 0;
    }
    SetFocusHit(hits_[static_cast<size_t>(index)].id, true);
}

void MainWindow::Activate(Hit id) {
    switch (id) {
        case Hit::SettingsButton:
            ShowSettingsPage(0);
            break;
        case Hit::MoreButton:
            ShowMoreMenu();
            break;
        case Hit::MinimizeButton:
            ShowWindow(hwnd_, SW_MINIMIZE);
            break;
        case Hit::MaximizeButton:
            ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
            break;
        case Hit::CloseButton:
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            break;
        case Hit::TargetCard:
            ShowTargetPicker();
            break;
        case Hit::AudioHelp:
            ShowAudioHelp();
            break;
        case Hit::SystemAudioToggle:
            OnToggleSystemAudio();
            break;
        case Hit::MicToggle:
            OnToggleMicrophone();
            break;
        case Hit::OutputDevice:
            OnDeviceMenu(true);
            break;
        case Hit::InputDevice:
            OnDeviceMenu(false);
            break;
        case Hit::Quality:
            OnQualityMenu();
            break;
        case Hit::SavePath:
            OnOpenOutputFolder();
            break;
        case Hit::Browse:
            OnBrowseClicked();
            break;
        case Hit::PreviewToggle:
            OnTogglePreview();
            break;
        case Hit::Start:
            OnStartClicked();
            break;
        case Hit::PauseResume:
            OnPauseResumeClicked();
            break;
        case Hit::Stop:
            OnStopClicked();
            break;
        case Hit::ResultOpenFile:
            OnResultOpenFile();
            break;
        case Hit::ResultOpenFolder:
            OnResultOpenFolder();
            break;
        case Hit::ResultAgain:
            OnResultRecordAgain();
            break;
        case Hit::ErrorRetry:
            OnErrorRetry();
            break;
        case Hit::ErrorHelp:
            OnErrorHelp();
            break;
        case Hit::ErrorDiagnostics:
            OnDiagnosticsClicked();
            break;
        default:
            break;
    }
}

void MainWindow::LoadSettings() {
    SettingsStore store;
    Settings settings = store.Load(nullptr);
    uiLanguage_ = settings.uiLanguage;
    previewRatio_ = std::clamp(static_cast<float>(settings.previewPaneRatio), 0.20f, 0.80f);
    i18n::SetLanguage(i18n::LanguageFromSetting(uiLanguage_));
    outputDirectory_ = settings.outputDirectory;
    if (outputDirectory_.empty()) outputDirectory_ = DefaultOutputDirectory();
    std::error_code ec;
    std::filesystem::create_directories(outputDirectory_, ec);
    preset_ = settings.preset == QualityPresetName::Low      ? QualityPreset::Low
              : settings.preset == QualityPresetName::High   ? QualityPreset::High
                                                             : QualityPreset::Standard;
    encoderPreference_ = encoder::EncoderPreferenceFromString(settings.encoderPreference);
    systemAudio_ = settings.recordSystemAudio;
    microphone_ = settings.recordMicrophone;
    showPreview_ = settings.showPreview;
    captureCursor_ = settings.captureCursor;
    minimizeToTray_ = settings.minimizeToTray;
    showNotification_ = settings.showCompletionNotification;
    autoOpenFolder_ = settings.autoOpenOutputFolder;
    autoCheckUpdates_ = settings.autoCheckUpdates;
    lastUpdateCheckUnix_ = settings.lastUpdateCheckUnix;
    lastNotifiedUpdateVersion_ = settings.lastNotifiedUpdateVersion;
    RefreshAudioDevices(settings.audioOutputDeviceId, settings.audioInputDeviceId);
    Application::Instance().SetMinimizeToTray(minimizeToTray_);
    Application::Instance().SetOutputDirectory(outputDirectory_);
}

void MainWindow::ApplySettingsFromStore() {
    SettingsStore store;
    Settings settings = store.Load(nullptr);
    const i18n::Language newLanguage =
        i18n::LanguageFromSetting(settings.uiLanguage);
    const bool languageChanged = newLanguage != i18n::CurrentLanguage();
    uiLanguage_ = settings.uiLanguage;
    if (!draggingSplitter_) {
        previewRatio_ =
            std::clamp(static_cast<float>(settings.previewPaneRatio), 0.20f, 0.80f);
    }
    i18n::SetLanguage(newLanguage);
    if (languageChanged) {
        RecreateFonts();
        lastTrayKey_ = 0;
    }
    outputDirectory_ = settings.outputDirectory;
    if (outputDirectory_.empty()) outputDirectory_ = DefaultOutputDirectory();
    std::error_code ec;
    std::filesystem::create_directories(outputDirectory_, ec);
    preset_ = settings.preset == QualityPresetName::Low      ? QualityPreset::Low
              : settings.preset == QualityPresetName::High   ? QualityPreset::High
                                                             : QualityPreset::Standard;
    encoderPreference_ = encoder::EncoderPreferenceFromString(settings.encoderPreference);
    systemAudio_ = settings.recordSystemAudio;
    microphone_ = settings.recordMicrophone;
    showPreview_ = settings.showPreview;
    captureCursor_ = settings.captureCursor;
    minimizeToTray_ = settings.minimizeToTray;
    showNotification_ = settings.showCompletionNotification;
    autoOpenFolder_ = settings.autoOpenOutputFolder;
    autoCheckUpdates_ = settings.autoCheckUpdates;
    lastUpdateCheckUnix_ = settings.lastUpdateCheckUnix;
    lastNotifiedUpdateVersion_ = settings.lastNotifiedUpdateVersion;
    if (preview_ != nullptr) {
        preview_->SetEnabled(showPreview_);
        if (!showPreview_) preview_->Clear();
    }
    RefreshAudioDevices(settings.audioOutputDeviceId, settings.audioInputDeviceId);
    Application::Instance().SetMinimizeToTray(minimizeToTray_);
    Application::Instance().SetOutputDirectory(outputDirectory_);
    if (showPreview_ && target_ != nullptr && preview_ != nullptr &&
        !preview_->HasFrame()) {
        StartPreviewForTarget();
    }
    UpdateWindowTitle();
    UpdateLayout();
    InvalidateAll();
    tray_.Update(lastSnapshot_);
}

void MainWindow::PersistSettings() {
    SettingsStore store;
    Settings settings = store.Load(nullptr);
    settings.outputDirectory = outputDirectory_;
    settings.preset = preset_ == QualityPreset::Low      ? QualityPresetName::Low
                      : preset_ == QualityPreset::High   ? QualityPresetName::High
                                                         : QualityPresetName::Standard;
    settings.encoderPreference = encoderPreference_ == encoder::EncoderPreference::Hardware
                                     ? "hardware"
                                 : encoderPreference_ == encoder::EncoderPreference::Software
                                     ? "software"
                                     : "auto";
    settings.recordSystemAudio = systemAudio_;
    settings.recordMicrophone = microphone_;
    settings.audioOutputDeviceId = SelectedAudioDeviceId(true);
    settings.audioInputDeviceId = SelectedAudioDeviceId(false);
    settings.showPreview = showPreview_;
    settings.captureCursor = captureCursor_;
    settings.minimizeToTray = minimizeToTray_;
    settings.showCompletionNotification = showNotification_;
    settings.autoOpenOutputFolder = autoOpenFolder_;
    settings.autoCheckUpdates = autoCheckUpdates_;
    settings.lastUpdateCheckUnix = lastUpdateCheckUnix_;
    settings.lastNotifiedUpdateVersion = lastNotifiedUpdateVersion_;
    settings.uiLanguage = uiLanguage_;
    settings.previewPaneRatio = previewRatio_;
    if (!store.Save(settings)) {
        log::Warning("UI", "SETTINGS_SAVE_FAILED", L"", L"Saving settings.json failed.");
    }
    Application::Instance().SetMinimizeToTray(minimizeToTray_);
    Application::Instance().SetOutputDirectory(outputDirectory_);
}

void MainWindow::RefreshWindowList() {
    const HWND previous = target_;
    for (const auto& info : windows_) {
        if (info.icon != nullptr) DestroyIcon(info.icon);
    }
    windows_ = window::WindowEnumerator().Enumerate();

    if (previous != nullptr) {
        bool found = false;
        for (const auto& info : windows_) {
            if (info.hwnd == previous) {
                found = true;
                targetPid_ = info.pid;
                targetTitle_ = info.title;
                targetExe_ = info.exeName;
                targetIcon_.reset();
                if (info.icon != nullptr) {
                    targetIcon_ = std::unique_ptr<Gdiplus::Bitmap>(
                        Gdiplus::Bitmap::FromHICON(info.icon));
                }
                break;
            }
        }
        if (!found) ClearTarget();
    }
}

void MainWindow::RefreshAudioDevices(const std::wstring& preferredOutput,
                                     const std::wstring& preferredInput) {
    audio::ComScope com;
    outputDevices_ = audio::EnumerateEndpoints(eRender);
    inputDevices_ = audio::EnumerateEndpoints(eCapture);
    defaultOutputName_ = audio::DefaultEndpointName(eRender);
    defaultInputName_ = audio::DefaultEndpointName(eCapture);

    const std::wstring outputId =
        preferredOutput.empty() ? SelectedAudioDeviceId(true) : preferredOutput;
    const std::wstring inputId =
        preferredInput.empty() ? SelectedAudioDeviceId(false) : preferredInput;
    SelectAudioDevice(true, outputId);
    SelectAudioDevice(false, inputId);
}

std::wstring MainWindow::SelectedAudioDeviceId(bool output) const {
    const auto& list = output ? outputDevices_ : inputDevices_;
    const int selection = output ? outputSelection_ : inputSelection_;
    if (selection > 0 && static_cast<size_t>(selection) <= list.size()) {
        return list[static_cast<size_t>(selection) - 1].id;
    }
    return L"";
}

void MainWindow::SelectAudioDevice(bool output, const std::wstring& id) {
    const auto& list = output ? outputDevices_ : inputDevices_;
    int selection = 0;
    if (!id.empty()) {
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].id == id) {
                selection = static_cast<int>(i) + 1;
                break;
            }
        }
    }
    if (output) outputSelection_ = selection;
    else inputSelection_ = selection;
}

std::wstring MainWindow::AudioDeviceLabel(bool output) const {
    const auto& list = output ? outputDevices_ : inputDevices_;
    const int selection = output ? outputSelection_ : inputSelection_;
    if (selection > 0 && static_cast<size_t>(selection) <= list.size()) {
        return list[static_cast<size_t>(selection) - 1].name;
    }
    const std::wstring& defaultName = output ? defaultOutputName_ : defaultInputName_;
    if (defaultName.empty()) return i18n::Text(i18n::Str::DefaultDevicePrefix);
    return i18n::Format(i18n::Str::DefaultDeviceLabel, {defaultName});
}

std::wstring MainWindow::QualityLabel() const {
    switch (preset_) {
        case QualityPreset::Low: return i18n::Text(i18n::Str::QualityLow);
        case QualityPreset::High: return i18n::Text(i18n::Str::QualityHigh);
        default: return i18n::Text(i18n::Str::QualityStandard);
    }
}

int MainWindow::QualityIndex() const {
    return preset_ == QualityPreset::Low ? 0 : (preset_ == QualityPreset::High ? 2 : 1);
}

void MainWindow::SetQualityIndex(int index) {
    preset_ = index == 0 ? QualityPreset::Low
             : index == 2 ? QualityPreset::High
                          : QualityPreset::Standard;
}

int MainWindow::EncoderIndex() const {
    switch (encoderPreference_) {
        case encoder::EncoderPreference::Hardware: return 1;
        case encoder::EncoderPreference::Software: return 2;
        default: return 0;
    }
}

void MainWindow::SetEncoderIndex(int index) {
    encoderPreference_ = index == 1 ? encoder::EncoderPreference::Hardware
                         : index == 2 ? encoder::EncoderPreference::Software
                                      : encoder::EncoderPreference::Auto;
}

std::wstring MainWindow::EncoderLabel() const {
    switch (encoderPreference_) {
        case encoder::EncoderPreference::Hardware:
            return i18n::Text(i18n::Str::EncoderHardware);
        case encoder::EncoderPreference::Software:
            return i18n::Text(i18n::Str::EncoderSoftware);
        default:
            return i18n::Text(i18n::Str::EncoderAuto);
    }
}

void MainWindow::ShowSettingsPage(int page) {
    if (!settings_) {
        settings_ = std::make_unique<SettingsWindow>(hwnd_, [this] {
            if (hwnd_ != nullptr) PostMessageW(hwnd_, WM_APP_SETTINGS_CHANGED, 0, 0);
        });
    }
    settings_->Show(static_cast<SettingsWindow::Page>(page));
}

void MainWindow::ShowCompressWindow() {
    if (!compressor_) {
        compressor_ = std::make_unique<CompressWindow>(hwnd_, [this] {
            const AppState state = coordinator_.GetStatus().state;
            return state == AppState::Preparing || state == AppState::Recording ||
                   state == AppState::Paused || state == AppState::Stopping;
        });
    }
    compressor_->Show();
}

void MainWindow::QueueCompressFiles(const std::vector<std::wstring>& files) {
    if (!compressor_) ShowCompressWindow();
    if (compressor_) compressor_->QueueFiles(files);
}

void MainWindow::StartUpdateCheck(bool manual) {
    if (updateCheckRunning_.exchange(true)) return;
    const HWND target = hwnd_;
    std::thread([target, manual] {
        auto result = update::CheckForUpdate(VSR_APP_VERSION_UTF8);
        auto* payload = new UpdateCheckPayload();
        payload->manual = manual;
        if (result) {
            payload->ok = true;
            payload->available = result.Value().available;
            payload->release = result.Value().release;
        } else {
            payload->error = result.GetError().userMessage;
        }
        if (!PostMessageW(target, WM_APP_UPDATE_CHECK_RESULT, 0,
                          reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

void MainWindow::ShowUpdateDialog() {
    if (!updateAvailable_) {
        StartUpdateCheck(true);
        return;
    }
    const AppState state = coordinator_.GetStatus().state;
    if (state == AppState::Preparing || state == AppState::Recording ||
        state == AppState::Paused || state == AppState::Stopping) {
        MessageBoxW(hwnd_, i18n::Text(i18n::Str::UpdateWhileRecording),
                    i18n::Text(i18n::Str::UpdateDownloadTitle),
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (updateWindow_ && updateWindow_->Hwnd() != nullptr) {
        SetForegroundWindow(updateWindow_->Hwnd());
        return;
    }

    const std::wstring body = i18n::Format(
        i18n::Str::UpdateAvailableBody, {VSR_APP_VERSION_W, updateVersion_});
    const int choice = MessageBoxW(hwnd_, body.c_str(),
                                   i18n::Text(i18n::Str::UpdateAvailableTitle),
                                   MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1);
    if (choice != IDYES) {
        if (!updateRelease_.htmlUrl.empty()) {
            ShellExecuteW(nullptr, L"open", updateRelease_.htmlUrl.c_str(), nullptr,
                          nullptr, SW_SHOWNORMAL);
        }
        return;
    }
    OpenUpdateWindow();
}

void MainWindow::OpenUpdateWindow() {
    if (!updateAvailable_) return;
    const AppState state = coordinator_.GetStatus().state;
    if (state == AppState::Preparing || state == AppState::Recording ||
        state == AppState::Paused || state == AppState::Stopping) {
        MessageBoxW(hwnd_, i18n::Text(i18n::Str::UpdateWhileRecording),
                    i18n::Text(i18n::Str::UpdateDownloadTitle),
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (updateWindow_ && updateWindow_->Hwnd() != nullptr) {
        SetForegroundWindow(updateWindow_->Hwnd());
        return;
    }
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const std::filesystem::path installDir =
        std::filesystem::path(exePath).parent_path();
    if (!update::IsDirectoryWritable(installDir)) {
        MessageBoxW(hwnd_, i18n::Text(i18n::Str::UpdateNotWritableBody),
                    i18n::Text(i18n::Str::UpdateFailedTitle),
                    MB_OK | MB_ICONWARNING);
        if (!updateRelease_.htmlUrl.empty()) {
            ShellExecuteW(nullptr, L"open", updateRelease_.htmlUrl.c_str(), nullptr,
                          nullptr, SW_SHOWNORMAL);
        }
        return;
    }
    if (!updateWindow_) {
        updateWindow_ = std::make_unique<UpdateWindow>(
            hwnd_, updateRelease_,
            [this](const std::filesystem::path& zip) { ApplyUpdateAndExit(zip); });
    }
    updateWindow_->Show();
}

void MainWindow::ApplyUpdateAndExit(const std::filesystem::path& zipPath) {
    const AppState state = coordinator_.GetStatus().state;
    if (state == AppState::Preparing || state == AppState::Recording ||
        state == AppState::Paused || state == AppState::Stopping) {
        MessageBoxW(hwnd_, i18n::Text(i18n::Str::UpdateWhileRecording),
                    i18n::Text(i18n::Str::UpdateDownloadTitle),
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const std::filesystem::path exe = exePath;
    const std::filesystem::path installDir = exe.parent_path();

    auto script = update::WriteApplyScript(zipPath, installDir, exe,
                                           GetCurrentProcessId());
    if (!script) {
        MessageBoxW(hwnd_, script.GetError().userMessage.c_str(),
                    i18n::Text(i18n::Str::UpdateFailedTitle), MB_OK | MB_ICONERROR);
        return;
    }
    auto launched = update::LaunchApplyScript(script.Value());
    if (!launched) {
        MessageBoxW(hwnd_, launched.GetError().userMessage.c_str(),
                    i18n::Text(i18n::Str::UpdateFailedTitle), MB_OK | MB_ICONERROR);
        return;
    }
    log::Info("Update", "UPDATE_INSTALLING", L"", zipPath.filename().wstring());
    DestroyWindow(hwnd_);
}

void MainWindow::ShowMoreMenu() {
    std::vector<menu::Item> items;
    items.push_back({i18n::Text(i18n::Str::TraySettings), L"", ui::Icon::Gear,
                     kCmdMoreSettings, true, false, false, false});
    items.push_back({i18n::Text(i18n::Str::CompressMenu), L"", ui::Icon::Video,
                     kCmdMoreCompress, true, false, false, false});
    items.push_back(
        {updateAvailable_ ? i18n::Format(i18n::Str::UpdateMenuAvailable, {updateVersion_})
                          : std::wstring(i18n::Text(i18n::Str::UpdateMenuCheck)),
         L"", updateAvailable_ ? ui::Icon::External : ui::Icon::Refresh, kCmdMoreUpdate,
         true, false, false, false});
    items.push_back({i18n::Text(i18n::Str::TrayHelp), L"", ui::Icon::Help,
                     kCmdMoreHelp, true, false, false, false});
    items.push_back({i18n::Text(i18n::Str::About), L"", ui::Icon::Info,
                     kCmdMoreAbout, true, false, false, false});
    items.push_back({L"", L"", ui::Icon::None, 0, true, false, false, true});
    items.push_back({i18n::Text(i18n::Str::CreateDiagnostics), L"",
                     ui::Icon::Diagnostics, kCmdMoreDiagnostics, true, false, false,
                     false});
    items.push_back({L"", L"", ui::Icon::None, 0, true, false, false, true});
    items.push_back({i18n::Text(i18n::Str::Exit), L"", ui::Icon::Exit, kCmdMoreExit,
                     true, false, false, false});

    POINT point = {static_cast<LONG>(layout_.more.X),
                   static_cast<LONG>(layout_.more.GetBottom())};
    ClientToScreen(hwnd_, &point);
    const UINT command = static_cast<UINT>(
        menu::Show(hwnd_, point, items, fonts_, true));
    switch (command) {
        case kCmdMoreSettings:
            ShowSettingsPage(0);
            break;
        case kCmdMoreCompress:
            ShowCompressWindow();
            break;
        case kCmdMoreUpdate:
            ShowUpdateDialog();
            break;
        case kCmdMoreHelp:
            OnErrorHelp();
            break;
        case kCmdMoreAbout:
            ShowSettingsPage(3);
            break;
        case kCmdMoreDiagnostics:
            OnDiagnosticsClicked();
            break;
        case kCmdMoreExit:
            if (coordinator_.GetStatus().state == AppState::Stopping) {
                MessageBoxW(hwnd_, i18n::Text(i18n::Str::SavingCloseBlocked),
                            i18n::Text(i18n::Str::AppName), MB_OK | MB_ICONINFORMATION);
                break;
            }
            {
                const StatusSnapshot snap = coordinator_.GetStatus();
                const bool active = snap.state == AppState::Recording ||
                                    snap.state == AppState::Paused ||
                                    snap.state == AppState::Preparing;
                if (active) {
                    const int choice = MessageBoxW(
                        hwnd_, i18n::Text(i18n::Str::ConfirmExitRecordingBody),
                        i18n::Text(i18n::Str::ConfirmExitRecordingTitle),
                        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
                    if (choice != IDYES) break;
                    (void)coordinator_.Stop(StopReason::User);
                }
                DestroyWindow(hwnd_);
            }
            break;
        default:
            break;
    }
}

void MainWindow::ShowTrayMenu() {
    const StatusSnapshot snap = coordinator_.GetStatus();
    const bool active = snap.state == AppState::Recording || snap.state == AppState::Paused;
    const std::wstring elapsed = i18n::FormatDuration(snap.activeElapsedMs);
    std::vector<menu::Item> items;
    items.push_back({i18n::Text(i18n::Str::TrayOpen), L"", ui::Icon::Window,
                     kCmdTrayOpen, true, false, false, false});
    if (snap.state == AppState::Recording) {
        items.push_back({i18n::Text(i18n::Str::TrayPause), L"", ui::Icon::Pause,
                         kCmdTrayToggle, true, false, false, false});
    } else if (snap.state == AppState::Paused) {
        items.push_back({i18n::Text(i18n::Str::TrayResume), L"", ui::Icon::Play,
                         kCmdTrayToggle, true, false, false, false});
    }
    items.push_back(
        {i18n::Text(i18n::Str::TrayStop),
         active ? i18n::Format(snap.state == AppState::Paused
                                   ? i18n::Str::TrayPausedElapsed
                                   : i18n::Str::TrayRecordingElapsed,
                               {elapsed})
                : L"",
         ui::Icon::Square, kCmdTrayStop, active, false, false, false});
    items.push_back({L"", L"", ui::Icon::None, 0, true, false, false, true});
    items.push_back({i18n::Text(i18n::Str::TraySettings), L"", ui::Icon::Gear,
                     kCmdTraySettings, true, false, false, false});
    items.push_back({i18n::Text(i18n::Str::CompressMenu), L"", ui::Icon::Video,
                     kCmdTrayCompress, true, false, false, false});
    items.push_back(
        {updateAvailable_ ? i18n::Format(i18n::Str::UpdateMenuAvailable, {updateVersion_})
                          : std::wstring(i18n::Text(i18n::Str::UpdateMenuCheck)),
         L"", updateAvailable_ ? ui::Icon::External : ui::Icon::Refresh, kCmdTrayUpdate,
         true, false, false, false});
    items.push_back({i18n::Text(i18n::Str::TrayHelp), L"", ui::Icon::Help,
                     kCmdTrayHelp, true, false, false, false});
    items.push_back({L"", L"", ui::Icon::None, 0, true, false, false, true});
    items.push_back({i18n::Text(i18n::Str::TrayExit), L"", ui::Icon::Exit, kCmdTrayExit,
                     true, false, false, false});

    POINT point = {};
    GetCursorPos(&point);
    const UINT command = static_cast<UINT>(menu::Show(hwnd_, point, items, fonts_, false));
    switch (command) {
        case kCmdTrayOpen:
            ShowAndActivate();
            break;
        case kCmdTrayToggle:
            if (snap.state == AppState::Recording) (void)coordinator_.Pause();
            else (void)coordinator_.Resume();
            RefreshUi();
            break;
        case kCmdTrayStop:
            OnStopClicked();
            break;
        case kCmdTraySettings:
            ShowSettingsPage(0);
            break;
        case kCmdTrayCompress:
            ShowCompressWindow();
            break;
        case kCmdTrayUpdate:
            ShowUpdateDialog();
            break;
        case kCmdTrayHelp:
            OnErrorHelp();
            break;
        case kCmdTrayExit: {
            if (snap.state == AppState::Stopping) {
                MessageBoxW(hwnd_, i18n::Text(i18n::Str::SavingCloseBlocked),
                            i18n::Text(i18n::Str::AppName), MB_OK | MB_ICONINFORMATION);
                break;
            }
            if (snap.state == AppState::Recording || snap.state == AppState::Paused ||
                snap.state == AppState::Preparing) {
                const int choice = MessageBoxW(
                    hwnd_, i18n::Text(i18n::Str::ConfirmExitRecordingBody),
                    i18n::Text(i18n::Str::ConfirmExitRecordingTitle),
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
                if (choice != IDYES) break;
                (void)coordinator_.Stop(StopReason::User);
            }
            DestroyWindow(hwnd_);
            break;
        }
        default:
            break;
    }
}

void MainWindow::ShowAudioHelp() {
    MessageBoxW(hwnd_, i18n::Text(i18n::Str::DeviceHintBody),
                i18n::Text(i18n::Str::DeviceHintTitle), MB_OK | MB_ICONINFORMATION);
}

void MainWindow::ShowTargetPicker() {
    if (picker_) {
        picker_->Show();
        return;
    }
    RefreshWindowList();
    if (windows_.empty()) {
        MessageBoxW(hwnd_, i18n::Text(i18n::Str::NoWindows),
                    i18n::Text(i18n::Str::NoTargetPrompt), MB_OK | MB_ICONINFORMATION);
        return;
    }
    picker_ = std::make_unique<WindowPicker>(hwnd_, windows_, target_,
                                             WM_APP_PICKER_RESULT);
    if (!picker_->Show()) {
        picker_.reset();
        return;
    }
    EnableWindow(hwnd_, FALSE);
}

void MainWindow::OnPickerResult(int index) {
    picker_.reset();
    EnableWindow(hwnd_, TRUE);
    SetForegroundWindow(hwnd_);
    if (index >= 0 && static_cast<size_t>(index) < windows_.size()) {
        SelectTarget(static_cast<size_t>(index));
    }
    UpdateLayout();
    InvalidateAll();
}

void MainWindow::SelectTarget(size_t index) {
    if (index >= windows_.size()) return;
    const window::WindowInfo& info = windows_[index];
    target_ = info.hwnd;
    targetPid_ = info.pid;
    targetTitle_ = info.title;
    targetExe_ = info.exeName;
    targetIcon_.reset();
    if (info.icon != nullptr) {
        targetIcon_ = std::unique_ptr<Gdiplus::Bitmap>(
            Gdiplus::Bitmap::FromHICON(info.icon));
    }
    thumbnailPending_ = false;
    thumbnailFailed_ = false;
    if (preview_ != nullptr) {
        preview_->Clear();
        preview_->SetEnabled(showPreview_);
        StartPreviewForTarget();
    }
    UpdateLayout();
    InvalidateAll();
}

void MainWindow::StartPreviewForTarget() {
    if (!showPreview_ || target_ == nullptr || preview_ == nullptr) return;
    // DWM thumbnails are live and work for GPU-composited apps where
    // PrintWindow returns a black surface; keep PrintWindow as the fallback.
    if (preview_->ShowThumbnail(target_)) {
        thumbnailPending_ = false;
        thumbnailFailed_ = false;
        return;
    }
    thumbnailPending_ = true;
    thumbnailFailed_ = false;
    thumbnailRequestTick_ = GetTickCount64();
    RequestThumbnail(target_);
}

void MainWindow::ClearTarget() {
    target_ = nullptr;
    targetPid_ = 0;
    targetTitle_.clear();
    targetExe_.clear();
    targetIcon_.reset();
    thumbnailPending_ = false;
    thumbnailFailed_ = false;
    if (preview_ != nullptr) {
        preview_->Clear();
    }
    UpdateLayout();
    InvalidateAll();
}

void MainWindow::OnStartClicked() {
    if (target_ == nullptr) {
        MessageBoxW(hwnd_, i18n::Text(i18n::Str::NoTargetPromptBody),
                    i18n::Text(i18n::Str::NoTargetPrompt), MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!window::WindowEnumerator::IsTargetAlive(target_, targetPid_)) {
        ClearTarget();
        MessageBoxW(hwnd_, i18n::Text(i18n::Str::TargetUnavailable),
                    i18n::Text(i18n::Str::NoTargetPrompt), MB_OK | MB_ICONWARNING);
        return;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(outputDirectory_, ec)) {
        MessageBoxW(hwnd_, i18n::Text(i18n::Str::PathInvalidBody),
                    i18n::Text(i18n::Str::PathInvalidTitle), MB_OK | MB_ICONWARNING);
        return;
    }

    StartRecordingRequest request;
    request.targetWindow = target_;
    request.outputDirectory = outputDirectory_;
    request.preset = preset_;
    request.encoderPreference = encoderPreference_;
    request.captureAudio = systemAudio_;
    request.captureMicrophone = microphone_;
    request.captureCursor = captureCursor_;
    request.audioOutputDeviceId = SelectedAudioDeviceId(true);
    request.audioInputDeviceId = SelectedAudioDeviceId(false);
    {
        SettingsStore store;
        bool usedDefaults = false;
        request.backend =
            encoder::WriterBackendFromString(store.Load(&usedDefaults).encoderBackend);
    }
    PersistSettings();
    lastActiveElapsedMs_ = 0;
    auto result = coordinator_.Start(request);
    if (!result) {
        const Error error = result.TakeError();
        EnterErrorPage(error.code, error.userMessage);
        return;
    }
    RefreshUi();
}

void MainWindow::OnPauseResumeClicked() {
    const StatusSnapshot snap = coordinator_.GetStatus();
    if (snap.state == AppState::Recording) {
        (void)coordinator_.Pause();
    } else if (snap.state == AppState::Paused) {
        (void)coordinator_.Resume();
    }
    RefreshUi();
}

void MainWindow::OnStopClicked() {
    OnStopAsync();
}

void MainWindow::OnStopAsync() {
    if (stopOpRunning_.load()) return;
    if (stopOp_.joinable()) stopOp_.join();
    stopOpRunning_.store(true);
    stopOpDone_.store(false);
    try {
        stopOp_ = std::thread([this] {
            (void)coordinator_.Stop(StopReason::User);
            stopOpDone_.store(true);
        });
    } catch (...) {
        stopOpRunning_.store(false);
        stopOpDone_.store(false);
        (void)coordinator_.Stop(StopReason::User);
        RefreshUi();
    }
}

void MainWindow::OnBrowseClicked() {
    if (browseRunning_.exchange(true)) return;
    const HWND owner = hwnd_;
    const std::wstring initial = outputDirectory_;
    std::thread([owner, initial] {
        std::wstring picked;
        const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        IFileDialog* dialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&dialog))) &&
            dialog != nullptr) {
            DWORD options = 0;
            dialog->GetOptions(&options);
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            dialog->SetTitle(i18n::Text(i18n::Str::BrowseForFolder));
            if (!initial.empty()) {
                IShellItem* folder = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr,
                                                          IID_PPV_ARGS(&folder))) &&
                    folder != nullptr) {
                    dialog->SetFolder(folder);
                    folder->Release();
                }
            }
            if (SUCCEEDED(dialog->Show(nullptr))) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) &&
                        path != nullptr) {
                        picked.assign(path);
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
            dialog->Release();
        }
        if (hrInit == S_OK || hrInit == S_FALSE) CoUninitialize();
        auto* payload = new std::wstring(std::move(picked));
        if (!PostMessageW(owner, WM_APP_BROWSE_RESULT, 0,
                          reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

void MainWindow::OnOpenOutputFolder() {
    OpenPath(outputDirectory_, false);
}

void MainWindow::OpenPath(const std::wstring& path, bool selectFile) {
    if (path.empty()) return;
    if (selectFile) {
        const std::wstring args = L"/select,\"" + path + L"\"";
        ShellExecuteW(hwnd_, L"open", L"explorer.exe", args.c_str(), nullptr,
                      SW_SHOWNORMAL);
    } else {
        ShellExecuteW(hwnd_, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void MainWindow::OnDiagnosticsClicked() {
    DiagnosticsContext context;
    context.appVersion = L"1.1.0.0";
    context.osBuild = Application::OsBuildString();
    auto package = CreateDiagnosticsPackage(context);
    if (package) {
        const std::wstring path = package.Value().wstring();
        const std::wstring body =
            i18n::Format(i18n::Str::DiagnosticsCreatedBody, {path});
        MessageBoxW(hwnd_, body.c_str(), i18n::Text(i18n::Str::DiagnosticsCreated),
                    MB_OK | MB_ICONINFORMATION);
    } else {
        const std::wstring logs = LogDir().wstring();
        const std::wstring body = i18n::Format(i18n::Str::DiagnosticsFailedBody, {logs});
        MessageBoxW(hwnd_, body.c_str(), i18n::Text(i18n::Str::DiagnosticsFailed),
                    MB_OK | MB_ICONWARNING);
    }
    PersistSettings();
}

void MainWindow::OnToggleSystemAudio() {
    if (lastSnapshot_.state != AppState::Ready) return;
    systemAudio_ = !systemAudio_;
    PersistSettings();
    InvalidateAll();
}

void MainWindow::OnToggleMicrophone() {
    if (lastSnapshot_.state != AppState::Ready) return;
    microphone_ = !microphone_;
    PersistSettings();
    InvalidateAll();
}

void MainWindow::OnDeviceMenu(bool output) {
    if (lastSnapshot_.state != AppState::Ready) return;
    const auto& devices = output ? outputDevices_ : inputDevices_;
    const std::wstring defaultName = output ? defaultOutputName_ : defaultInputName_;
    std::vector<menu::Item> items;
    const int selection = output ? outputSelection_ : inputSelection_;
    std::wstring defaultLabel = i18n::Text(i18n::Str::DefaultDevicePrefix);
    if (!defaultName.empty()) {
        defaultLabel = i18n::Format(i18n::Str::DefaultDeviceLabel, {defaultName});
    }
    items.push_back({defaultLabel, L"", ui::Icon::None, kCmdDeviceBase, true,
                     selection == 0, false, false});
    for (size_t i = 0; i < devices.size(); ++i) {
        items.push_back({devices[i].name, L"", ui::Icon::None,
                         kCmdDeviceBase + static_cast<UINT>(i + 1), true,
                         selection == static_cast<int>(i) + 1, false, false});
    }
    if (devices.empty() && defaultName.empty()) {
        items.clear();
        items.push_back({i18n::Text(i18n::Str::AudioNoDevices), L"", ui::Icon::None, 0,
                         false, false, false, false});
    }

    const Gdiplus::RectF control =
        output ? layout_.audioDevice[0] : layout_.audioDevice[1];
    POINT point = {static_cast<LONG>(control.X),
                   static_cast<LONG>(control.GetBottom())};
    ClientToScreen(hwnd_, &point);
    const UINT command =
        static_cast<UINT>(menu::Show(hwnd_, point, items, fonts_, false));
    if (command < kCmdDeviceBase) return;
    const size_t index = command - kCmdDeviceBase;
    if (index == 0) {
        SelectAudioDevice(output, L"");
    } else if (index <= devices.size()) {
        SelectAudioDevice(output, devices[index - 1].id);
    }
    PersistSettings();
    InvalidateAll();
}

void MainWindow::OnQualityMenu() {
    if (lastSnapshot_.state != AppState::Ready) return;
    std::vector<menu::Item> items;
    items.push_back({i18n::Text(i18n::Str::QualityLow), L"", ui::Icon::None,
                     kCmdQualityBase, true, QualityIndex() == 0, false, false});
    items.push_back({i18n::Text(i18n::Str::QualityStandard), L"", ui::Icon::None,
                     kCmdQualityBase + 1, true, QualityIndex() == 1, false, false});
    items.push_back({i18n::Text(i18n::Str::QualityHigh), L"", ui::Icon::None,
                     kCmdQualityBase + 2, true, QualityIndex() == 2, false, false});
    POINT point = {static_cast<LONG>(layout_.qualityValue.X),
                   static_cast<LONG>(layout_.qualityValue.GetBottom())};
    ClientToScreen(hwnd_, &point);
    const UINT command =
        static_cast<UINT>(menu::Show(hwnd_, point, items, fonts_, false));
    if (command < kCmdQualityBase) return;
    SetQualityIndex(static_cast<int>(command - kCmdQualityBase));
    PersistSettings();
    InvalidateAll();
}

void MainWindow::OnTogglePreview() {
    showPreview_ = !showPreview_;
    if (preview_ != nullptr) {
        preview_->SetEnabled(showPreview_);
        if (!showPreview_) {
            preview_->Clear();
        } else if (target_ != nullptr && !preview_->HasFrame()) {
            StartPreviewForTarget();
        }
    }
    PersistSettings();
    UpdateLayout();
    InvalidateAll();
}

void MainWindow::OnResultOpenFile() {
    if (!resultPath_.empty()) OpenPath(resultPath_, false);
}

void MainWindow::OnResultOpenFolder() {
    if (!resultPath_.empty()) OpenPath(resultPath_, true);
}

void MainWindow::OnResultRecordAgain() {
    if (coordinator_.GetStatus().state == AppState::Faulted) {
        (void)coordinator_.Reset();
    }
    resultPath_.clear();
    resultName_.clear();
    resultThumb_.clear();
    resultThumbWidth_ = 0;
    resultThumbHeight_ = 0;
    resultHadError_ = false;
    resultErrorCode_ = ErrorCode::None;
    resultTargetLost_ = false;
    errorCode_ = ErrorCode::None;
    errorDetail_.clear();
    thumbnailPending_ = false;
    thumbnailFailed_ = false;
    if (preview_ != nullptr) {
        preview_->Clear();
        StartPreviewForTarget();
    }
    EnterMainPage();
    focus_ = Hit::Start;
    keyboardFocus_ = false;
    InvalidateAll();
}

void MainWindow::OnErrorRetry() {
    const ErrorCode code = errorCode_;
    if (coordinator_.GetStatus().state == AppState::Faulted) {
        (void)coordinator_.Reset();
    }
    const bool canRetry = target_ != nullptr &&
                          window::WindowEnumerator::IsTargetAlive(target_, targetPid_) &&
                          IsRetryableAfterStartFailure(code);
    errorCode_ = ErrorCode::None;
    errorDetail_.clear();
    EnterMainPage();
    if (canRetry) OnStartClicked();
    else {
        focus_ = target_ != nullptr ? Hit::Start : Hit::TargetCard;
        keyboardFocus_ = false;
        InvalidateAll();
    }
}

void MainWindow::OnErrorHelp() {
    MessageBoxW(hwnd_, i18n::Text(i18n::Str::HelpBody),
                i18n::Text(i18n::Str::HelpTitle), MB_OK | MB_ICONINFORMATION);
}

void MainWindow::EnterMainPage() {
    page_ = Page::Main;
    ExitCompact();
    UpdateLayout();
    InvalidateAll();
}

void MainWindow::EnterResultPage(const StatusSnapshot& snap) {
    page_ = Page::Result;
    EnterCompact(320);
    UpdateLayout();
    InvalidateAll();
    if (showNotification_) {
        const std::wstring title = resultHadError_ ? i18n::Text(i18n::Str::StoppedErrorTitle)
                                                   : i18n::Text(i18n::Str::ToastSavedTitle);
        const std::wstring second = i18n::FormatDuration(resultDurationMs_) + L"  ·  " +
                                    i18n::FormatBytes(resultBytes_);
        ShowToast(title, resultName_, second, resultHadError_);
    }
    if (autoOpenFolder_ && !resultHadError_ && !resultPath_.empty()) {
        OpenPath(resultPath_, true);
    }
    (void)snap;
}

void MainWindow::EnterErrorPage(const StatusSnapshot& snap) {
    errorCode_ = snap.errorCode;
    errorDetail_ = snap.errorUserMessage;
    page_ = Page::Error;
    EnterCompact(380);
    UpdateLayout();
    InvalidateAll();
}

void MainWindow::EnterErrorPage(ErrorCode code, const std::wstring& detail) {
    errorCode_ = code;
    errorDetail_ = detail;
    page_ = Page::Error;
    EnterCompact(380);
    UpdateLayout();
    InvalidateAll();
}

void MainWindow::FinishRecording(const StatusSnapshot& snap) {
    std::wstring path = snap.finalPath;
    if (!IsFileUsable(path)) path = snap.partialPath;
    lastActiveElapsedMs_ = std::max(lastActiveElapsedMs_, snap.activeElapsedMs);
    if (!IsFileUsable(path)) {
        if (snap.hasError || snap.state == AppState::Faulted) EnterErrorPage(snap);
        else EnterMainPage();
        return;
    }

    resultPath_ = path;
    resultName_ = BaseName(path);
    resultDurationMs_ = lastActiveElapsedMs_;
    resultBytes_ = FileSize(path);
    // An MP4 that contains no encoded frames is not a usable recording even
    // though the file exists (seen with windows that PrintWindow cannot
    // render). Report it as a failed result instead of "complete".
    const bool emptyRecording = snap.encodedFrames == 0 && resultBytes_ < 4096;
    resultHadError_ = snap.hasError || emptyRecording;
    resultErrorCode_ = snap.errorCode;
    resultTargetLost_ = target_ != nullptr &&
                        !window::WindowEnumerator::IsTargetAlive(target_, targetPid_);
    resultThumb_.clear();
    resultThumbWidth_ = 0;
    resultThumbHeight_ = 0;
    if (preview_ != nullptr) {
        preview_->CopyLastFrame(resultThumb_, resultThumbWidth_, resultThumbHeight_);
    }
    EnterResultPage(snap);
}

void MainWindow::EnterCompact(int logicalHeight) {
    if (hwnd_ == nullptr) return;
    if (!compact_ && !IsZoomed(hwnd_)) {
        GetWindowRect(hwnd_, &normalRect_);
        normalRectValid_ = true;
    }
    compact_ = true;
    if (IsZoomed(hwnd_)) return;
    RECT window = {};
    GetWindowRect(hwnd_, &window);
    int width = window.right - window.left;
    int height = ui::Scale(logicalHeight, dpi_);
    int left = window.left;
    int top = window.top;
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info)) {
        const int maxHeight = info.rcWork.bottom - info.rcWork.top;
        height = std::min(height, maxHeight);
        if (top + height > info.rcWork.bottom) top = info.rcWork.bottom - height;
        if (top < info.rcWork.top) top = info.rcWork.top;
    }
    SetWindowPos(hwnd_, nullptr, left, top, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void MainWindow::ExitCompact() {
    if (!compact_) return;
    compact_ = false;
    if (normalRectValid_ && !IsZoomed(hwnd_)) {
        SetWindowPos(hwnd_, nullptr, normalRect_.left, normalRect_.top,
                     normalRect_.right - normalRect_.left,
                     normalRect_.bottom - normalRect_.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    normalRectValid_ = false;
}

void MainWindow::RequestThumbnail(HWND target) {
    if (target == nullptr || preview_ == nullptr || thumbGeneration_ == nullptr) return;
    const int generation = thumbGeneration_->fetch_add(1) + 1;
    const HWND main = hwnd_;
    std::thread([main, target, generation] {
        auto* payload = new ThumbnailPayload();
        payload->generation = generation;
        payload->failed = !MainWindow::CaptureWindowThumbnail(
            target, payload->bgra, payload->width, payload->height);
        if (!PostMessageW(main, WM_APP_THUMBNAIL, 0,
                          reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

bool MainWindow::CaptureWindowThumbnail(HWND window, std::vector<std::uint8_t>& outBgra,
                                        UINT& outWidth, UINT& outHeight) {
    if (window == nullptr || !IsWindow(window)) return false;
    RECT client = {};
    if (!GetClientRect(window, &client)) return false;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return false;
    if (static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) >
        16ull * 1024ull * 1024ull) {
        return false;
    }

    HDC screen = GetDC(nullptr);
    if (screen == nullptr) return false;
    HDC memory = CreateCompatibleDC(screen);
    if (memory == nullptr) {
        ReleaseDC(nullptr, screen);
        return false;
    }
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (bitmap != nullptr && bits != nullptr) {
        HGDIOBJ old = SelectObject(memory, bitmap);
        // PW_CLIENTONLY | PW_RENDERFULLCONTENT matches the capture backend
        // used for recording; fall back to the plain client render if the app
        // refuses the full-content flag.
        if (!PrintWindow(window, memory, 1 | 2)) {
            (void)PrintWindow(window, memory, 1);
        }
        SelectObject(memory, old);
        outBgra.assign(static_cast<size_t>(width) * height * 4, 0);
        memcpy(outBgra.data(), bits, outBgra.size());
        for (size_t i = 3; i < outBgra.size(); i += 4) outBgra[i] = 255;
        bool any = false;
        for (size_t i = 0; i < outBgra.size(); i += 4) {
            if (outBgra[i] != 0 || outBgra[i + 1] != 0 || outBgra[i + 2] != 0) {
                any = true;
                break;
            }
        }
        if (any) {
            outWidth = static_cast<UINT>(width);
            outHeight = static_cast<UINT>(height);
            ok = true;
        }
    }
    if (bitmap != nullptr) DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return ok;
}

std::wstring MainWindow::ElapsedText() const {
    return i18n::FormatDuration(lastSnapshot_.activeElapsedMs);
}

std::wstring MainWindow::ActiveTargetName() const {
    if (!targetTitle_.empty()) return targetTitle_;
    if (!targetExe_.empty()) return targetExe_;
    return i18n::Text(i18n::Str::NotAvailable);
}

std::wstring MainWindow::FileSizeText() const {
    return i18n::FormatBytes(resultBytes_);
}

bool MainWindow::IsActiveState(AppState state) const {
    return state == AppState::Preparing || state == AppState::Recording ||
           state == AppState::Paused || state == AppState::Stopping;
}

void MainWindow::UpdateTray() {
    const UINT key = static_cast<UINT>(lastSnapshot_.state) * 100000u +
                     static_cast<UINT>(lastSnapshot_.activeElapsedMs / 1000);
    if (key == lastTrayKey_) return;
    lastTrayKey_ = key;
    tray_.Update(lastSnapshot_);
}

void MainWindow::RefreshLevelMonitor() {
    // The meters must be useful BEFORE recording starts: the whole point is to
    // confirm the selected window/audio devices while setting up. Run a
    // shared-mode WASAPI capture for the meters only; stop it as soon as the
    // session takes over the endpoints (state Preparing) or the window leaves
    // the foreground/minimizes.
    const bool visible = hwnd_ != nullptr && IsWindowVisible(hwnd_) && !IsIconic(hwnd_);
    const bool wanted = page_ == Page::Main && lastSnapshot_.state == AppState::Ready &&
                        target_ != nullptr && visible &&
                        (systemAudio_ || microphone_);
    if (!wanted) {
        if (levelMonitor_ != nullptr) {
            levelMonitor_->Stop();
            levelMonitor_.reset();
        }
        levelMonitorKey_.clear();
        previewLoopbackLevel_.store(0.0f, std::memory_order_relaxed);
        previewMicLevel_.store(0.0f, std::memory_order_relaxed);
        return;
    }

    const std::wstring outputId = SelectedAudioDeviceId(true);
    const std::wstring inputId = SelectedAudioDeviceId(false);
    const std::wstring key =
        std::wstring(systemAudio_ ? L"1" : L"0") + (microphone_ ? L"1" : L"0") +
        outputId + L"|" + inputId;
    if (levelMonitor_ != nullptr && key == levelMonitorKey_) return;
    if (levelMonitor_ == nullptr && key == levelMonitorKey_ &&
        GetTickCount64() < levelMonitorRetryTick_) {
        return;
    }

    if (levelMonitor_ != nullptr) {
        levelMonitor_->Stop();
        levelMonitor_.reset();
    }
    levelMonitorKey_ = key;
    // Avoid retry/log spam when an endpoint is temporarily unavailable.
    levelMonitorRetryTick_ = GetTickCount64() + 15000;

    encoder::AudioCaptureOptions options;
    options.loopback = systemAudio_;
    options.microphone = microphone_;
    options.loopbackDeviceId = outputId;
    options.microphoneDeviceId = inputId;
    options.onLevel = [this](bool microphone, float peak) {
        (microphone ? previewMicLevel_ : previewLoopbackLevel_)
            .store(peak, std::memory_order_relaxed);
    };
    auto started = encoder::AudioCapture::Start(
        levelMonitorClock_, [](encoder::AudioChunk&&) {}, options);
    if (!started) {
        const Error error = started.TakeError();
        log::Warning("UI", "LEVEL_MONITOR_UNAVAILABLE", L"",
                     L"Idle audio level monitor could not start.",
                     {{"code", ToString(error.code)},
                      {"detail", WideToUtf8(error.technicalMessage)}});
        previewLoopbackLevel_.store(0.0f, std::memory_order_relaxed);
        previewMicLevel_.store(0.0f, std::memory_order_relaxed);
        return;
    }
    levelMonitor_ = std::move(started.Value());
}

void MainWindow::ShowToast(const std::wstring& title, const std::wstring& line1,
                           const std::wstring& line2, bool warning, bool liveElapsed) {
    if (!toast_) toast_ = std::make_unique<ToastWindow>();
    toastLiveElapsed_ = liveElapsed;
    toast_->Show(hwnd_, title, line1, line2, warning);
    lastToastKey_ = title + L"|" + line2;
}

void MainWindow::InvalidateAll() {
    if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
}

} // namespace vsr::app
