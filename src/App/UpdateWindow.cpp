#include "UpdateWindow.h"

#include <windowsx.h>

#include <dwmapi.h>

#include <algorithm>
#include <utility>

#include <Infrastructure/Log.h>
#include <Infrastructure/Strings.h>
#include <Update/UpdateApplier.h>
#include <Update/UpdateChecker.h>

#include "Localization.h"

#pragma comment(lib, "dwmapi.lib")

namespace vsr::app {

namespace {

constexpr wchar_t kUpdateWindowClass[] = L"VsrUpdateWindow";
constexpr int kAppIconResource = 1;
constexpr UINT WM_APP_UPDATE_DONE = WM_APP + 20;

struct DonePayload {
    bool ok = false;
    std::filesystem::path path;
    std::wstring error;
};

bool Contains(const Gdiplus::RectF& rect, POINT point) {
    return static_cast<float>(point.x) >= rect.X &&
           static_cast<float>(point.x) < rect.X + rect.Width &&
           static_cast<float>(point.y) >= rect.Y &&
           static_cast<float>(point.y) < rect.Y + rect.Height;
}

} // namespace

UpdateWindow::UpdateWindow(HWND owner, update::ReleaseInfo release, ReadyCallback onReady)
    : owner_(owner), release_(std::move(release)), onReady_(std::move(onReady)) {}

UpdateWindow::~UpdateWindow() {
    cancel_.store(true);
    if (worker_.joinable()) worker_.join();
    if (memBmp_ != nullptr) DeleteObject(memBmp_);
    if (memDc_ != nullptr) DeleteDC(memDc_);
}

void UpdateWindow::Show() {
    if (!CreateWindowIfNeeded()) return;
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
    StartDownload();
}

bool UpdateWindow::CreateWindowIfNeeded() {
    if (hwnd_ != nullptr) return true;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &UpdateWindow::WndProcThunk;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kUpdateWindowClass;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(kAppIconResource));
    RegisterClassExW(&wc);

    dpi_ = owner_ != nullptr ? GetDpiForWindow(owner_) : GetDpiForSystem();
    if (dpi_ == 0) dpi_ = 96;
    fonts_.Create(dpi_, i18n::CurrentLanguage());

    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST), &info);
    const int workWidth = info.rcWork.right - info.rcWork.left;
    const int workHeight = info.rcWork.bottom - info.rcWork.top;
    const int width = std::min(ui::Scale(560, dpi_), workWidth - ui::Scale(32, dpi_));
    const int height = std::min(ui::Scale(300, dpi_), workHeight - ui::Scale(32, dpi_));
    const int x = info.rcWork.left + (workWidth - width) / 2;
    const int y = info.rcWork.top + (workHeight - height) / 2;

    const std::wstring title = std::wstring(i18n::Text(i18n::Str::AppName)) + L" - " +
                               i18n::Text(i18n::Str::UpdateDownloadTitle);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kUpdateWindowClass, title.c_str(),
                            WS_POPUP | WS_CLIPCHILDREN, x, y, width, height, owner_,
                            nullptr, instance, this);
    if (hwnd_ == nullptr) {
        log::Error("UI", "UPDATE_WINDOW_FAILED", L"",
                   L"CreateWindowExW failed for the update window.",
                   {{"error", std::to_string(GetLastError())}});
        return false;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    UpdateLayout();
    SetTimer(hwnd_, pollTimer_, 200, nullptr);
    return true;
}

void UpdateWindow::StartDownload() {
    const std::filesystem::path destination =
        update::UpdatesDirectory() / Utf8ToWide(release_.version.ToString());
    worker_ = std::thread([this, destination] {
        auto result = update::DownloadVerified(
            release_, destination, cancel_,
            [this](std::uint64_t downloaded, std::uint64_t total) {
                downloaded_.store(downloaded);
                total_.store(total);
            });
        auto* payload = new DonePayload();
        if (result) {
            payload->ok = true;
            payload->path = result.Value();
        } else {
            payload->error = result.GetError().userMessage;
        }
        if (!PostMessageW(hwnd_, WM_APP_UPDATE_DONE, 0,
                          reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    });
}

void UpdateWindow::OnDownloadDone(bool ok, const std::filesystem::path& path,
                                  const std::wstring& error) {
    if (worker_.joinable()) worker_.join();
    if (cancel_.load()) {
        state_ = State::Cancelled;
        Invalidate();
        return;
    }
    if (ok) {
        zipPath_ = path;
        state_ = State::Ready;
        Invalidate();
        SetTimer(hwnd_, applyTimer_, 800, nullptr);
        return;
    }
    state_ = State::Failed;
    error_ = error;
    log::Warning("Update", "DOWNLOAD_FAILED", L"", error);
    Invalidate();
}

void UpdateWindow::PollProgress() {
    if (state_ != State::Downloading) return;
    Invalidate();
}

void UpdateWindow::RequestClose() {
    if (state_ == State::Downloading) {
        cancel_.store(true);
        state_ = State::Cancelled;
        Invalidate();
    }
    if (hwnd_ != nullptr) DestroyWindow(hwnd_);
}

void UpdateWindow::OnDestroy() {
    if (hwnd_ != nullptr) {
        KillTimer(hwnd_, pollTimer_);
        KillTimer(hwnd_, applyTimer_);
    }
    cancel_.store(true);
    if (worker_.joinable()) worker_.join();
    if (memBmp_ != nullptr) {
        DeleteObject(memBmp_);
        memBmp_ = nullptr;
    }
    if (memDc_ != nullptr) {
        DeleteDC(memDc_);
        memDc_ = nullptr;
    }
    memWidth_ = 0;
    memHeight_ = 0;
    fonts_.Destroy();
    hwnd_ = nullptr;
    hits_.clear();
}

LRESULT CALLBACK UpdateWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<UpdateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self != nullptr ? self->WndProc(hwnd, msg, wp, lp)
                           : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT UpdateWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCCALCSIZE:
        case WM_NCPAINT:
            return 0;
        case WM_NCACTIVATE:
            return TRUE;
        case WM_NCHITTEST: {
            POINT screen = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            POINT client = screen;
            ScreenToClient(hwnd, &client);
            if (Contains(layout_.titleBar, client) && !Contains(layout_.close, client)) {
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        case WM_DPICHANGED: {
            auto* suggested = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            dpi_ = HIWORD(wp);
            fonts_.Create(dpi_, i18n::CurrentLanguage());
            UpdateLayout();
            Invalidate();
            return 0;
        }
        case WM_SIZE:
            UpdateLayout();
            Invalidate();
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
        case WM_PRINTCLIENT:
            Paint(reinterpret_cast<HDC>(wp));
            return 0;
        case WM_TIMER:
            if (wp == pollTimer_) PollProgress();
            else if (wp == applyTimer_) {
                KillTimer(hwnd, applyTimer_);
                if (onReady_) onReady_(zipPath_);
            }
            return 0;
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
            const Hit hit = HitTest(point);
            if (hit != hover_) {
                hover_ = hit;
                Invalidate();
            }
            SetCursor(LoadCursorW(nullptr, hit != Hit::None ? IDC_HAND : IDC_ARROW));
            return 0;
        }
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            hover_ = Hit::None;
            Invalidate();
            return 0;
        case WM_LBUTTONDOWN: {
            POINT point = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            pressed_ = HitTest(point);
            if (pressed_ != Hit::None) SetCapture(hwnd);
            Invalidate();
            return 0;
        }
        case WM_LBUTTONUP: {
            const Hit pressed = pressed_;
            pressed_ = Hit::None;
            if (GetCapture() == hwnd) ReleaseCapture();
            POINT point = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (pressed != Hit::None && HitTest(point) == pressed) {
                if (pressed == Hit::Close) RequestClose();
                else if (pressed == Hit::Cancel) {
                    cancel_.store(true);
                    state_ = State::Cancelled;
                    Invalidate();
                } else if (pressed == Hit::ApplyNow && state_ == State::Ready &&
                           onReady_) {
                    onReady_(zipPath_);
                }
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            pressed_ = Hit::None;
            Invalidate();
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) RequestClose();
            return 0;
        case WM_APP_UPDATE_DONE: {
            DonePayload* payload = reinterpret_cast<DonePayload*>(lp);
            if (payload != nullptr) {
                OnDownloadDone(payload->ok, payload->path, payload->error);
                delete payload;
            }
            return 0;
        }
        case WM_CLOSE:
            RequestClose();
            return 0;
        case WM_DESTROY:
            OnDestroy();
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void UpdateWindow::Invalidate() {
    if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
}

void UpdateWindow::UpdateLayout() {
    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    const float width = static_cast<float>(rc.right - rc.left);
    const float height = static_cast<float>(rc.bottom - rc.top);
    if (width <= 0 || height <= 0) return;
    const float pad = ui::ScaleF(24.0f, dpi_);
    const float titleHeight = ui::ScaleF(44.0f, dpi_);

    layout_.titleBar = Gdiplus::RectF(0, 0, width, titleHeight);
    layout_.title = Gdiplus::RectF(ui::ScaleF(52.0f, dpi_), 0,
                                   width - ui::ScaleF(120.0f, dpi_), titleHeight);
    layout_.close = Gdiplus::RectF(width - ui::ScaleF(52.0f, dpi_),
                                   ui::ScaleF(8.0f, dpi_), ui::ScaleF(32.0f, dpi_),
                                   ui::ScaleF(28.0f, dpi_));
    layout_.heading = Gdiplus::RectF(pad, titleHeight + ui::ScaleF(18.0f, dpi_),
                                     width - pad * 2.0f, ui::ScaleF(28.0f, dpi_));
    layout_.status = Gdiplus::RectF(pad, layout_.heading.GetBottom() +
                                             ui::ScaleF(8.0f, dpi_),
                                    width - pad * 2.0f, ui::ScaleF(22.0f, dpi_));
    layout_.progress = Gdiplus::RectF(pad, layout_.status.GetBottom() +
                                               ui::ScaleF(18.0f, dpi_),
                                      width - pad * 2.0f, ui::ScaleF(10.0f, dpi_));
    layout_.detail = Gdiplus::RectF(pad, layout_.progress.GetBottom() +
                                             ui::ScaleF(10.0f, dpi_),
                                    width - pad * 2.0f, ui::ScaleF(20.0f, dpi_));
    layout_.button = Gdiplus::RectF(width - pad - ui::ScaleF(110.0f, dpi_),
                                    height - ui::ScaleF(56.0f, dpi_),
                                    ui::ScaleF(110.0f, dpi_),
                                    ui::ScaleF(34.0f, dpi_));

    hits_.clear();
    hits_.push_back({Hit::Close, layout_.close});
    hits_.push_back({Hit::Cancel, layout_.button});
}

void UpdateWindow::Paint(HDC targetDc) {
    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;
    if (memDc_ == nullptr) memDc_ = CreateCompatibleDC(targetDc);
    if (memBmp_ == nullptr || memWidth_ != width || memHeight_ != height) {
        if (memBmp_ != nullptr) DeleteObject(memBmp_);
        memBmp_ = CreateCompatibleBitmap(targetDc, width, height);
        memWidth_ = width;
        memHeight_ = height;
    }
    if (memDc_ == nullptr || memBmp_ == nullptr) return;
    HGDIOBJ old = SelectObject(memDc_, memBmp_);
    {
        Gdiplus::Graphics graphics(memDc_);
        Draw(graphics);
    }
    BitBlt(targetDc, 0, 0, width, height, memDc_, 0, 0, SRCCOPY);
    SelectObject(memDc_, old);
}

void UpdateWindow::Draw(Gdiplus::Graphics& graphics) {
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    graphics.Clear(ui::colors::Window());

    ui::FillRect(graphics, layout_.titleBar, ui::colors::TitleBar());
    ui::DrawLine(graphics, 0.0f, layout_.titleBar.GetBottom() - 1.0f,
                 layout_.titleBar.GetRight(), layout_.titleBar.GetBottom() - 1.0f,
                 ui::colors::Border());
    const float logoSize = ui::ScaleF(22.0f, dpi_);
    ui::DrawLogo(graphics,
                 Gdiplus::RectF(ui::ScaleF(20.0f, dpi_),
                                (layout_.titleBar.Height - logoSize) / 2.0f, logoSize,
                                logoSize),
                 ui::ScaleF(6.0f, dpi_));
    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::UpdateDownloadTitle),
                 layout_.title, ui::FontRole::BodyStrong, ui::colors::Text(),
                 ui::Align::Near, ui::VAlign::Center, true);
    if (hover_ == Hit::Close) {
        ui::FillRoundRect(graphics, layout_.close, ui::ScaleF(6.0f, dpi_),
                          ui::colors::HoverTint());
    }
    const float closeGlyph = ui::ScaleF(14.0f, dpi_);
    ui::DrawIcon(graphics, ui::Icon::Close,
                 Gdiplus::RectF(layout_.close.X + (layout_.close.Width - closeGlyph) / 2.0f,
                                layout_.close.Y + (layout_.close.Height - closeGlyph) / 2.0f,
                                closeGlyph, closeGlyph),
                 ui::colors::TextSecondary(), ui::ScaleF(1.6f, dpi_));

    const std::wstring version = Utf8ToWide(release_.version.ToString());
    const std::wstring heading =
        state_ == State::Downloading
            ? i18n::Format(i18n::Str::UpdateDownloading, {version})
            : std::wstring(i18n::Text(i18n::Str::UpdateDownloadTitle));
    ui::DrawText(graphics, fonts_, heading, layout_.heading, ui::FontRole::Section,
                 ui::colors::Text(), ui::Align::Near, ui::VAlign::Center, true);

    const std::uint64_t downloaded = downloaded_.load();
    const std::uint64_t total = total_.load();
    std::wstring status;
    Gdiplus::Color statusColor = ui::colors::TextSecondary();
    switch (state_) {
        case State::Downloading:
            if (total > 0 && downloaded >= total) {
                status = i18n::Text(i18n::Str::UpdateVerifying);
            }
            break;
        case State::Ready:
            status = i18n::Text(i18n::Str::UpdateReady);
            statusColor = ui::colors::Success();
            break;
        case State::Failed:
            status = std::wstring(i18n::Text(i18n::Str::UpdateFailedTitle)) + L" · " +
                     error_;
            statusColor = ui::colors::DangerText();
            break;
        case State::Cancelled:
            status = i18n::Text(i18n::Str::UpdateCancelled);
            break;
    }
    ui::DrawText(graphics, fonts_, status, layout_.status, ui::FontRole::Small,
                 statusColor, ui::Align::Near, ui::VAlign::Center, true);

    const double fraction = total > 0
                                ? std::min(1.0, static_cast<double>(downloaded) /
                                                    static_cast<double>(total))
                                : 0.0;
    ui::FillRoundRect(graphics, layout_.progress, ui::ScaleF(5.0f, dpi_),
                      ui::colors::Border());
    if (fraction > 0.0) {
        ui::FillRoundRect(
            graphics,
            Gdiplus::RectF(layout_.progress.X, layout_.progress.Y,
                           std::max(ui::ScaleF(10.0f, dpi_),
                                    layout_.progress.Width * static_cast<float>(fraction)),
                           layout_.progress.Height),
            ui::ScaleF(5.0f, dpi_), ui::colors::Primary());
    }

    std::wstring detail;
    if (total > 0) {
        detail = i18n::Format(i18n::Str::UpdateProgress,
                              {i18n::FormatBytes(downloaded),
                               i18n::FormatBytes(total)});
        detail += L" · " +
                  std::to_wstring(static_cast<int>(fraction * 100.0 + 0.5)) + L"%";
    } else if (downloaded > 0) {
        detail = i18n::FormatBytes(downloaded);
    }
    if (!detail.empty()) {
        ui::DrawText(graphics, fonts_, detail, layout_.detail, ui::FontRole::Small,
                     ui::colors::TextSecondary(), ui::Align::Near, ui::VAlign::Center,
                     true);
    }

    if (state_ == State::Downloading) {
        ui::DrawButton(graphics, fonts_, layout_.button,
                       i18n::Text(i18n::Str::UpdateCancel), ui::ButtonStyle::Secondary,
                       hover_ == Hit::Cancel, pressed_ == Hit::Cancel, true);
    } else if (state_ == State::Ready) {
        ui::DrawButton(graphics, fonts_, layout_.button,
                       i18n::Text(i18n::Str::UpdateInstallNow),
                       ui::ButtonStyle::Primary, hover_ == Hit::ApplyNow,
                       pressed_ == Hit::ApplyNow, true);
    } else if (state_ == State::Failed || state_ == State::Cancelled) {
        ui::DrawButton(graphics, fonts_, layout_.button,
                       i18n::Text(i18n::Str::UpdateClose), ui::ButtonStyle::Secondary,
                       hover_ == Hit::Cancel, pressed_ == Hit::Cancel, true);
    }
}

UpdateWindow::Hit UpdateWindow::HitTest(POINT point) const {
    for (const HitTarget& target : hits_) {
        if (!Contains(target.rect, point)) continue;
        if (target.id == Hit::Cancel) {
            if (state_ == State::Ready) return Hit::ApplyNow;
            if (state_ != State::Downloading) return Hit::None;
        }
        return target.id;
    }
    return Hit::None;
}

} // namespace vsr::app
