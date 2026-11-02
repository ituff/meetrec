#include "ToastWindow.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>

#include "Application.h"

#pragma comment(lib, "dwmapi.lib")

namespace vsr::app {

namespace {
constexpr wchar_t kToastClassName[] = L"VsrToastWindow";
constexpr UINT kAutoHideMs = 5000;
constexpr int kAppIconResource = 1;
} // namespace

ToastWindow::~ToastWindow() {
    ReleaseBackBuffer();
    if (hwnd_ != nullptr) {
        KillTimer(hwnd_, hideTimerId_);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    fonts_.Destroy();
}

void ToastWindow::ReleaseBackBuffer() {
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
}

void ToastWindow::EnsureWindow() {
    if (hwnd_ != nullptr) return;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &ToastWindow::WndProcThunk;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kToastClassName;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(kAppIconResource));
    RegisterClassExW(&wc);

    dpi_ = owner_ != nullptr ? GetDpiForWindow(owner_) : GetDpiForSystem();
    if (dpi_ == 0) dpi_ = 96;
    const int width = ui::Scale(340, dpi_);
    const int height = ui::Scale(108, dpi_);
    hwnd_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                            kToastClassName, L"", WS_POPUP, 0, 0, width, height, owner_,
                            nullptr, instance, this);
    if (hwnd_ == nullptr) return;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    fonts_.Create(dpi_, i18n::CurrentLanguage());
}

void ToastWindow::Show(HWND owner, const std::wstring& title, const std::wstring& line1,
                       const std::wstring& line2, bool warning) {
    owner_ = owner;
    title_ = title;
    line1_ = line1;
    line2_ = line2;
    warning_ = warning;
    closeHover_ = false;
    EnsureWindow();
    if (hwnd_ == nullptr) return;
    fonts_.Create(dpi_, i18n::CurrentLanguage());
    Position();
    SetTimer(hwnd_, hideTimerId_, kAutoHideMs, nullptr);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ToastWindow::SetSecondLine(const std::wstring& line2) {
    if (line2 == line2_) return;
    line2_ = line2;
    if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
}

void ToastWindow::Hide() {
    if (hwnd_ == nullptr) return;
    KillTimer(hwnd_, hideTimerId_);
    ShowWindow(hwnd_, SW_HIDE);
}

bool ToastWindow::IsVisible() const {
    return hwnd_ != nullptr && IsWindowVisible(hwnd_);
}

void ToastWindow::Position() {
    if (hwnd_ == nullptr) return;
    RECT window = {};
    GetWindowRect(hwnd_, &window);
    const int width = window.right - window.left;
    const int height = window.bottom - window.top;
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    const HMONITOR monitor =
        MonitorFromWindow(owner_ != nullptr ? owner_ : hwnd_, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(monitor, &info)) {
        info.rcWork = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN),
                           GetSystemMetrics(SM_CYSCREEN)};
    }
    const int x = info.rcWork.right - width - ui::Scale(16, dpi_);
    const int y = info.rcWork.bottom - height - ui::Scale(16, dpi_);
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, ui::Scale(24, dpi_),
                                     ui::Scale(24, dpi_));
    SetWindowRgn(hwnd_, region, TRUE);
}

void ToastWindow::Paint(HDC targetDc) {
    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;
    if (memDc_ == nullptr || memWidth_ != width || memHeight_ != height) {
        ReleaseBackBuffer();
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
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        const auto S = [&](float value) { return ui::ScaleF(value, dpi_); };
        ui::FillRect(graphics, Gdiplus::RectF(0, 0, static_cast<float>(width),
                                               static_cast<float>(height)),
                     ui::colors::Surface());
        ui::DrawLogo(graphics, Gdiplus::RectF(S(16), S(12), S(20), S(20)), S(5));
        ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::AppName),
                     Gdiplus::RectF(S(44), S(8), S(180), S(26)), ui::FontRole::BodyStrong,
                     ui::colors::Text(), ui::Align::Near, ui::VAlign::Center, false);
        closeRect_ = Gdiplus::RectF(S(300), S(10), S(24), S(24));
        if (closeHover_) {
            ui::FillRoundRect(graphics, closeRect_, S(5), ui::colors::HoverTint());
        }
        ui::DrawIcon(graphics, ui::Icon::Close,
                     Gdiplus::RectF(closeRect_.X + S(6), closeRect_.Y + S(6), S(12),
                                    S(12)),
                     ui::colors::TextSecondary(), S(1.2f));
        ui::DrawLine(graphics, S(14), S(36), static_cast<float>(width) - S(14), S(36),
                     ui::colors::Divider(), 1.0f);

        const Gdiplus::Color accent =
            warning_ ? ui::colors::Warning() : ui::colors::Primary();
        const Gdiplus::RectF badge(S(18), S(50), S(36), S(36));
        ui::DrawCircle(graphics, badge, accent, Gdiplus::Color(0, 0, 0, 0), 0.0f);
        ui::DrawIcon(graphics, warning_ ? ui::Icon::Alert : ui::Icon::Info, badge,
                     warning_ ? ui::colors::Warning() : ui::colors::Primary(), S(1.6f));
        ui::DrawText(graphics, fonts_, title_,
                     Gdiplus::RectF(S(66), S(44), static_cast<float>(width) - S(80),
                                    S(22)),
                     ui::FontRole::BodyStrong, ui::colors::Text(), ui::Align::Near,
                     ui::VAlign::Center, true);
        ui::DrawText(graphics, fonts_, line1_,
                     Gdiplus::RectF(S(66), S(64), static_cast<float>(width) - S(80),
                                    S(18)),
                     ui::FontRole::Small, ui::colors::TextSecondary(), ui::Align::Near,
                     ui::VAlign::Center, true);
        ui::DrawText(graphics, fonts_, line2_,
                     Gdiplus::RectF(S(66), S(82), static_cast<float>(width) - S(80),
                                    S(18)),
                     ui::FontRole::Small, ui::colors::TextTertiary(), ui::Align::Near,
                     ui::VAlign::Center, false);
    }

    HDC dc = targetDc != nullptr ? targetDc : GetDC(hwnd_);
    BitBlt(dc, 0, 0, width, height, memDc_, 0, 0, SRCCOPY);
    if (targetDc == nullptr) ReleaseDC(hwnd_, dc);
}

LRESULT CALLBACK ToastWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<ToastWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self != nullptr ? self->WndProc(hwnd, msg, wp, lp)
                           : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ToastWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
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
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            if (wp == hideTimerId_) {
                Hide();
                return 0;
            }
            break;
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
            const bool hover = point.x >= closeRect_.X && point.x < closeRect_.GetRight() &&
                               point.y >= closeRect_.Y && point.y < closeRect_.GetBottom();
            if (hover != closeHover_) {
                closeHover_ = hover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            SetCursor(LoadCursorW(nullptr, hover ? IDC_HAND : IDC_ARROW));
            return 0;
        }
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            if (closeHover_) {
                closeHover_ = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP: {
            POINT point = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            const bool close = point.x >= closeRect_.X &&
                               point.x < closeRect_.GetRight() &&
                               point.y >= closeRect_.Y && point.y < closeRect_.GetBottom();
            if (!close && owner_ != nullptr) {
                PostMessageW(owner_, WM_APP + 1, 0, 0);
            }
            Hide();
            return 0;
        }
        case WM_DESTROY:
            hwnd_ = nullptr;
            ReleaseBackBuffer();
            fonts_.Destroy();
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace vsr::app
