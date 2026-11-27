#include "ModelDownloadWindow.h"

#include <windowsx.h>

#include <algorithm>

#include <Infrastructure/Log.h>

namespace vsr::app {

namespace {

constexpr wchar_t kDownloadClassName[] = L"VsrModelDownloadWindow";

Gdiplus::Color TrackColor() { return Gdiplus::Color(255, 232, 236, 242); }

} // namespace

ModelDownloadWindow::ModelDownloadWindow(HWND owner, DoneCallback onDone)
    : owner_(owner), onDone_(std::move(onDone)) {}

ModelDownloadWindow::~ModelDownloadWindow() {
    cancel_.store(true);
    if (worker_.joinable()) worker_.join();
    if (hwnd_ != nullptr) {
        KillTimer(hwnd_, timer_);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    ReleaseBackBuffer();
    fonts_.Destroy();
}

bool ModelDownloadWindow::EnsureWindow() {
    if (hwnd_ != nullptr) return true;

    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &ModelDownloadWindow::WndProcThunk;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kDownloadClassName;
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    dpi_ = owner_ != nullptr ? GetDpiForWindow(owner_) : GetDpiForSystem();
    if (dpi_ == 0) dpi_ = 96;
    fonts_.Create(dpi_, i18n::CurrentLanguage());

    const int width = ui::Scale(500, dpi_);
    const int height = ui::Scale(260, dpi_);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kDownloadClassName, L"",
                            WS_POPUP | WS_CLIPCHILDREN, 0, 0, width, height, owner_,
                            nullptr, instance, this);
    if (hwnd_ == nullptr) return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    UpdateLayout();
    return true;
}

void ModelDownloadWindow::Show() {
    if (!EnsureWindow()) return;
    EnableWindow(owner_, FALSE);

    RECT ownerRect = {};
    if (owner_ != nullptr && GetWindowRect(owner_, &ownerRect)) {
        RECT self = {};
        GetWindowRect(hwnd_, &self);
        const int width = self.right - self.left;
        const int height = self.bottom - self.top;
        const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
        const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
        SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    } else {
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    Start();
    SetTimer(hwnd_, timer_, 100, nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ModelDownloadWindow::Start() {
    state_ = State::Downloading;
    cancel_.store(false);
    cancelRequested_.store(false);
    done_.store(false);
    phase_.store(static_cast<int>(ModelInstallPhase::Preparing));
    percent_.store(0);
    downloaded_.store(0);
    total_.store(0);
    closeAfterCancel_ = false;
    callbackInvoked_ = false;

    worker_ = std::thread([this] {
        auto onProgress = [this](const ModelInstallProgress& p) {
            phase_.store(static_cast<int>(p.phase), std::memory_order_relaxed);
            percent_.store(p.percent, std::memory_order_relaxed);
            downloaded_.store(p.downloaded, std::memory_order_relaxed);
            total_.store(p.total, std::memory_order_relaxed);
        };
        ModelInstallResult result =
            DownloadAndInstallSpeechModel(cancel_, onProgress);
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            result_ = std::move(result);
        }
        done_.store(true, std::memory_order_release);
    });
}

void ModelDownloadWindow::Poll() {
    if (hwnd_ == nullptr || state_ != State::Downloading) return;
    if (!done_.load(std::memory_order_acquire)) {
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    ApplyResult();
}

void ModelDownloadWindow::ApplyResult() {
    if (worker_.joinable()) worker_.join();
    ModelInstallResult result;
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        result = result_;
    }

    if (result.ok) {
        state_ = State::Success;
    } else if (result.cancelled || cancelRequested_.load()) {
        state_ = State::Cancelled;
    } else {
        state_ = State::Failed;
    }
    KillTimer(hwnd_, timer_);
    if (!callbackInvoked_ && onDone_) {
        callbackInvoked_ = true;
        onDone_(result);
    }
    if (closeAfterCancel_ && hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        return;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ModelDownloadWindow::RequestCancel() {
    if (state_ != State::Downloading || cancelRequested_.exchange(true)) return;
    cancel_.store(true);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ModelDownloadWindow::RequestClose() {
    if (state_ == State::Downloading) {
        closeAfterCancel_ = true;
        RequestCancel();
        return;
    }
    if (hwnd_ != nullptr) DestroyWindow(hwnd_);
}

void ModelDownloadWindow::UpdateLayout() {
    if (hwnd_ == nullptr) return;
    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    const float width = static_cast<float>(rc.right - rc.left);
    const float height = static_cast<float>(rc.bottom - rc.top);
    if (width <= 0.0f || height <= 0.0f) return;
    const auto S = [&](int value) { return ui::ScaleF(static_cast<float>(value), dpi_); };

    Layout layout;
    layout.titleBar = Gdiplus::RectF(0, 0, width, S(42));
    layout.title = Gdiplus::RectF(S(16), S(6), width - S(80), S(30));
    layout.close = Gdiplus::RectF(width - S(44), 0, S(44), S(42));
    layout.heading = Gdiplus::RectF(S(20), S(54), width - S(40), S(24));
    layout.status = Gdiplus::RectF(S(20), S(84), width - S(40), S(20));
    layout.progress = Gdiplus::RectF(S(20), S(112), width - S(40), S(8));
    layout.detail = Gdiplus::RectF(S(20), S(128), width - S(40), S(60));
    layout.action = Gdiplus::RectF(width - S(20) - S(112), height - S(20) - S(38), S(112),
                                   S(38));
    layout_ = layout;
    HitTestBuild();
}

void ModelDownloadWindow::HitTestBuild() {
    hits_.clear();
    hits_.push_back({Hit::CloseButton, layout_.close});
    hits_.push_back({Hit::Action, layout_.action});
    auto stillValid = [&](Hit id) {
        return std::any_of(hits_.begin(), hits_.end(),
                           [id](const HitTarget& target) { return target.id == id; });
    };
    if (!stillValid(hover_)) hover_ = Hit::None;
    if (!stillValid(pressed_)) pressed_ = Hit::None;
}

ModelDownloadWindow::Hit ModelDownloadWindow::HitTest(POINT point) const {
    for (const HitTarget& target : hits_) {
        if (static_cast<float>(point.x) >= target.rect.X &&
            static_cast<float>(point.x) < target.rect.GetRight() &&
            static_cast<float>(point.y) >= target.rect.Y &&
            static_cast<float>(point.y) < target.rect.GetBottom()) {
            return target.id;
        }
    }
    return Hit::None;
}

std::wstring ModelDownloadWindow::StatusText() const {
    if (cancelRequested_.load() && state_ == State::Downloading) {
        return i18n::Text(i18n::Str::ModelDownloadStatusCancelling);
    }
    switch (state_) {
        case State::Success:
            return i18n::Text(i18n::Str::ModelDownloadStatusDone);
        case State::Failed:
            return i18n::Text(i18n::Str::ModelDownloadStatusFailed);
        case State::Cancelled:
            return i18n::Text(i18n::Str::ModelDownloadStatusCancelled);
        case State::Downloading:
            break;
    }
    switch (static_cast<ModelInstallPhase>(phase_.load())) {
        case ModelInstallPhase::Preparing:
            return i18n::Text(i18n::Str::ModelDownloadPreparing);
        case ModelInstallPhase::DownloadingModel:
            return i18n::Text(i18n::Str::ModelDownloadStatusModel);
        case ModelInstallPhase::VerifyingModel:
            return i18n::Text(i18n::Str::ModelDownloadStatusVerify);
        case ModelInstallPhase::DownloadingVad:
            return i18n::Text(i18n::Str::ModelDownloadStatusVad);
        case ModelInstallPhase::Extracting:
            return i18n::Text(i18n::Str::ModelDownloadStatusExtract);
        case ModelInstallPhase::Installing:
            return i18n::Text(i18n::Str::ModelDownloadStatusInstall);
        case ModelInstallPhase::Done:
            return i18n::Text(i18n::Str::ModelDownloadStatusDone);
    }
    return i18n::Text(i18n::Str::ModelDownloadPreparing);
}

std::wstring ModelDownloadWindow::DetailText() const {
    if (state_ == State::Failed) {
        std::wstring error = result_.error;
        if (error.size() > 220) {
            error.resize(220);
            error += L"…";
        }
        return error;
    }
    if (state_ == State::Success) {
        return i18n::Format(i18n::Str::ModelDownloadInstalledTo,
                            {result_.directory.wstring()});
    }
    if (state_ == State::Cancelled) return {};
    const ModelInstallPhase phase = static_cast<ModelInstallPhase>(phase_.load());
    const int percent = std::clamp(percent_.load(), 0, 100);
    if (phase == ModelInstallPhase::DownloadingModel ||
        phase == ModelInstallPhase::DownloadingVad) {
        return i18n::Format(i18n::Str::ModelDownloadBytes,
                            {i18n::FormatBytes(downloaded_.load()),
                             i18n::FormatBytes(total_.load())}) +
               L"  ·  " + std::to_wstring(percent) + L"%";
    }
    return std::to_wstring(percent) + L"%";
}

std::wstring ModelDownloadWindow::ActionLabel() const {
    switch (state_) {
        case State::Downloading:
            return i18n::Text(i18n::Str::ModelDownloadCancel);
        case State::Success:
            return i18n::Text(i18n::Str::ModelDownloadDone);
        case State::Failed:
        case State::Cancelled:
            return i18n::Text(i18n::Str::ModelDownloadClose);
    }
    return i18n::Text(i18n::Str::ModelDownloadClose);
}

ui::ButtonStyle ModelDownloadWindow::ActionStyle() const {
    return state_ == State::Success ? ui::ButtonStyle::Primary
                                    : ui::ButtonStyle::Secondary;
}

void ModelDownloadWindow::Activate(Hit hit) {
    switch (hit) {
        case Hit::CloseButton:
            RequestClose();
            break;
        case Hit::Action:
            if (state_ == State::Downloading) {
                RequestCancel();
            } else {
                RequestClose();
            }
            break;
        default:
            break;
    }
}

void ModelDownloadWindow::ReleaseBackBuffer() {
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

void ModelDownloadWindow::Paint(HDC targetDc) {
    if (hwnd_ == nullptr) return;
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
        Draw(graphics);
    }
    HDC dc = targetDc != nullptr ? targetDc : GetDC(hwnd_);
    BitBlt(dc, 0, 0, width, height, memDc_, 0, 0, SRCCOPY);
    if (targetDc == nullptr) ReleaseDC(hwnd_, dc);
}

void ModelDownloadWindow::Draw(Gdiplus::Graphics& graphics) {
    const auto S = [&](float value) { return ui::ScaleF(value, dpi_); };
    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    ui::FillRect(graphics,
                 Gdiplus::RectF(0, 0, static_cast<float>(rc.right),
                                static_cast<float>(rc.bottom)),
                 ui::colors::Window());
    ui::FillRect(graphics, layout_.titleBar, ui::colors::TitleBar());
    ui::DrawLine(graphics, 0, layout_.titleBar.GetBottom() - 0.5f,
                 static_cast<float>(rc.right), layout_.titleBar.GetBottom() - 0.5f,
                 ui::colors::Divider(), 1.0f);
    ui::DrawText(graphics, fonts_,
                 std::wstring(i18n::Text(i18n::Str::AppName)) + L" - " +
                     i18n::Text(i18n::Str::ModelDownloadTitle),
                 layout_.title, ui::FontRole::Title, ui::colors::Text(), ui::Align::Near,
                 ui::VAlign::Center, true);

    if (hover_ == Hit::CloseButton) {
        ui::FillRoundRect(graphics,
                          Gdiplus::RectF(layout_.close.X + S(4), layout_.close.Y + S(8),
                                         layout_.close.Width - S(8),
                                         layout_.close.Height - S(16)),
                          S(6), ui::colors::Danger());
    }
    ui::DrawIcon(graphics, ui::Icon::Close,
                 Gdiplus::RectF(layout_.close.X + (layout_.close.Width - S(14)) / 2,
                                layout_.close.Y + (layout_.close.Height - S(14)) / 2,
                                S(14), S(14)),
                 hover_ == Hit::CloseButton ? ui::colors::White()
                                            : ui::colors::TextSecondary(),
                 S(1.3f));

    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::ModelDownloadHeading),
                 layout_.heading, ui::FontRole::Section, ui::colors::Text(),
                 ui::Align::Near, ui::VAlign::Center, false);
    ui::DrawText(graphics, fonts_, StatusText(), layout_.status, ui::FontRole::Body,
                 ui::colors::TextSecondary(), ui::Align::Near, ui::VAlign::Center, true);

    ui::FillRoundRect(graphics, layout_.progress, S(4), TrackColor());
    const int percent = std::clamp(percent_.load(), 0, 100);
    if (percent > 0 && state_ != State::Failed && state_ != State::Cancelled) {
        ui::FillRoundRect(
            graphics,
            Gdiplus::RectF(layout_.progress.X, layout_.progress.Y,
                           layout_.progress.Width * static_cast<float>(percent) / 100.0f,
                           layout_.progress.Height),
            S(4), ui::colors::Primary());
    }

    ui::DrawTextWrapped(graphics, fonts_, DetailText(), layout_.detail,
                        ui::FontRole::Small,
                        state_ == State::Failed ? ui::colors::DangerText()
                                                : ui::colors::TextSecondary(),
                        ui::Align::Near);

    const bool actionEnabled = !(state_ == State::Downloading &&
                                 cancelRequested_.load());
    ui::DrawButton(graphics, fonts_, layout_.action, ActionLabel(), ActionStyle(),
                   hover_ == Hit::Action, pressed_ == Hit::Action, actionEnabled);
}

LRESULT CALLBACK ModelDownloadWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp,
                                                   LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self =
        reinterpret_cast<ModelDownloadWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self != nullptr ? self->WndProc(hwnd, msg, wp, lp)
                           : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ModelDownloadWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC dc = BeginPaint(hwnd, &ps);
            Paint(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER:
            if (wp == timer_) {
                Poll();
                return 0;
            }
            break;
        case WM_MOUSEMOVE:
            if (!trackingMouse_) {
                TRACKMOUSEEVENT track = {};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = hwnd;
                TrackMouseEvent(&track);
                trackingMouse_ = true;
            }
            hover_ = HitTest(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            hover_ = Hit::None;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
            pressed_ = HitTest(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            const Hit hit = HitTest(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            const Hit pressed = pressed_;
            pressed_ = Hit::None;
            if (hit == pressed) Activate(hit);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                RequestClose();
                return 0;
            }
            break;
        case WM_CLOSE:
            RequestClose();
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, timer_);
            hwnd_ = nullptr;
            ReleaseBackBuffer();
            if (owner_ != nullptr && IsWindow(owner_)) {
                EnableWindow(owner_, TRUE);
                SetForegroundWindow(owner_);
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace vsr::app
