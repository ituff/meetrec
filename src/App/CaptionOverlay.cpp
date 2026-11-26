#include "CaptionOverlay.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include <Infrastructure/Log.h>
#include <Infrastructure/Strings.h>
#include <Speech/SubtitleFormat.h>

namespace vsr::app {

namespace {

constexpr wchar_t kOverlayClassName[] = L"VsrCaptionOverlay";
constexpr UINT_PTR kOverlayTimerId = 0xCA97;
constexpr UINT kOverlayTimerMs = 100;
constexpr int kMaxWidthLogical = 760;
constexpr int kMinWidthLogical = 240;
constexpr int kPaddingLogical = 16;
constexpr int kLatestLineLogical = 30;
constexpr int kPreviousLineLogical = 18;
constexpr int kLineGapLogical = 6;
constexpr int kBottomMarginLogical = 24;
constexpr int kSideMarginLogical = 16;

Gdiplus::Font* MakeFont(const wchar_t* family, float size) {
    auto* font = new Gdiplus::Font(family, size, Gdiplus::FontStyleRegular,
                                   Gdiplus::UnitPixel);
    if (font->GetLastStatus() != Gdiplus::Ok) {
        delete font;
        font = new Gdiplus::Font(Gdiplus::FontFamily::GenericSansSerif(), size,
                                 Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    }
    return font;
}

std::wstring LimitLine(const std::wstring& line, bool truncated) {
    return truncated ? line + L"\u2026" : line;
}

} // namespace

CaptionOverlay::~CaptionOverlay() {
    DestroyWindowNow();
    ReleaseBackBuffer();
    delete captionFont_;
    delete previousFont_;
    captionFont_ = nullptr;
    previousFont_ = nullptr;
}

void CaptionOverlay::EnsureWindow() {
    if (hwnd_ != nullptr) return;

    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &CaptionOverlay::WndProcThunk;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kOverlayClassName;
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return;
    }

    const UINT dpi = target_ != nullptr ? GetDpiForWindow(target_) : GetDpiForSystem();
    RecreateFonts(dpi == 0 ? 96 : dpi);

    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
                                WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                            kOverlayClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr,
                            nullptr, instance, this);
    if (hwnd_ == nullptr) return;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetLayeredWindowAttributes(hwnd_, 0, 238, LWA_ALPHA);
    lastWidth_ = -1;
    lastHeight_ = -1;
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 1, 1,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void CaptionOverlay::DestroyWindowNow() {
    if (hwnd_ == nullptr) return;
    KillTimer(hwnd_, kOverlayTimerId);
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    shown_ = false;
}

void CaptionOverlay::RecreateFonts(UINT dpi) {
    if (dpi_ == dpi && captionFont_ != nullptr && previousFont_ != nullptr) return;
    delete captionFont_;
    delete previousFont_;
    dpi_ = dpi;
    captionFont_ = MakeFont(L"Microsoft YaHei UI", ui::ScaleF(22.0f, dpi_));
    previousFont_ = MakeFont(L"Microsoft YaHei UI", ui::ScaleF(13.0f, dpi_));
}

void CaptionOverlay::Show(HWND target, std::shared_ptr<speech::CaptionFeed> feed,
                          bool chinese) {
    target_ = target;
    feed_ = std::move(feed);
    chinese_ = chinese;
    paused_ = false;
    lastSequence_ = 0;
    cues_.clear();
    latestLines_.clear();
    previousLine_.clear();
    hasPrevious_ = false;
    shown_ = false;

    EnsureWindow();
    if (hwnd_ == nullptr) return;

    UpdateContent();
    SetTimer(hwnd_, kOverlayTimerId, kOverlayTimerMs, nullptr);
}

void CaptionOverlay::Hide() {
    target_ = nullptr;
    feed_.reset();
    paused_ = false;
    lastSequence_ = 0;
    cues_.clear();
    latestLines_.clear();
    previousLine_.clear();
    hasPrevious_ = false;
    if (hwnd_ != nullptr) KillTimer(hwnd_, kOverlayTimerId);
    SetShown(false);
}

void CaptionOverlay::SetPaused(bool paused) {
    paused_ = paused;
    if (paused_) {
        SetShown(false);
        return;
    }
    // Resume: rebuild from the current feed sequence.
    lastSequence_ = 0;
    UpdateContent();
}

bool CaptionOverlay::IsVisible() const {
    return hwnd_ != nullptr && shown_;
}

void CaptionOverlay::SetShown(bool shown) {
    if (hwnd_ == nullptr || shown == shown_) return;
    if (shown) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER |
                         SWP_SHOWWINDOW);
        InvalidateRect(hwnd_, nullptr, FALSE);
    } else {
        ShowWindow(hwnd_, SW_HIDE);
    }
    shown_ = shown;
}

void CaptionOverlay::UpdateContent() {
    if (hwnd_ == nullptr) return;
    if (paused_ || feed_ == nullptr) {
        SetShown(false);
        return;
    }

    const std::uint64_t sequence = feed_->Sequence();
    if (sequence == lastSequence_) return;
    lastSequence_ = sequence;

    cues_ = feed_->Snapshot(2);
    if (cues_.empty()) {
        latestLines_.clear();
        previousLine_.clear();
        hasPrevious_ = false;
        SetShown(false);
        return;
    }

    const speech::CaptionCue& latest = cues_.back();
    latestMicrophone_ = latest.microphone;
    const std::wstring latestText = speech::SanitizeTranscriptText(latest.text);
    latestLines_ = speech::WrapSubtitleText(latestText, 56);
    if (latestLines_.size() > 3) {
        latestLines_.resize(3);
        latestLines_.back() = LimitLine(latestLines_.back(), true);
    }
    if (latestLines_.empty()) {
        SetShown(false);
        return;
    }
    // Prefix the first wrapped line instead of wrapping "label + text" as one
    // string: the punctuation after the label is a break opportunity and
    // otherwise leaves "对方：" alone on its own line.
    latestLines_[0] =
        speech::SpeakerPrefix(latest.microphone, chinese_) + latestLines_[0];

    hasPrevious_ = cues_.size() > 1;
    previousLine_.clear();
    if (hasPrevious_) {
        const speech::CaptionCue& previous = cues_[cues_.size() - 2];
        const std::wstring previousText =
            speech::SanitizeTranscriptText(previous.text);
        const std::vector<std::wstring> lines =
            speech::WrapSubtitleText(previousText, 84);
        if (!lines.empty()) {
            previousLine_ = speech::SpeakerPrefix(previous.microphone, chinese_) +
                            LimitLine(lines.front(), lines.size() > 1);
        }
    }

    if (Position()) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

bool CaptionOverlay::Position() {
    if (hwnd_ == nullptr) return false;
    if (target_ == nullptr || !IsWindow(target_) || IsIconic(target_) ||
        !IsWindowVisible(target_)) {
        SetShown(false);
        return false;
    }
    if (latestLines_.empty()) {
        SetShown(false);
        return false;
    }

    const UINT targetDpi = GetDpiForWindow(target_);
    if (targetDpi != 0) RecreateFonts(targetDpi);

    RECT targetRect = {};
    if (!GetWindowRect(target_, &targetRect)) {
        SetShown(false);
        return false;
    }
    const int targetWidth = targetRect.right - targetRect.left;

    const int width = std::max(
        ui::Scale(kMinWidthLogical, dpi_),
        std::min(ui::Scale(kMaxWidthLogical, dpi_), targetWidth - ui::Scale(32, dpi_)));

    int height = ui::Scale(kPaddingLogical * 2, dpi_);
    if (hasPrevious_) {
        height += ui::Scale(kPreviousLineLogical, dpi_) + ui::Scale(kLineGapLogical, dpi_);
    }
    height += static_cast<int>(latestLines_.size()) *
              ui::Scale(kLatestLineLogical, dpi_);

    int x = targetRect.left + (targetWidth - width) / 2;
    int y = targetRect.bottom - height - ui::Scale(kBottomMarginLogical, dpi_);

    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    const HMONITOR monitor = MonitorFromWindow(target_, MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(monitor, &info)) {
        x = std::max<int>(info.rcWork.left + ui::Scale(8, dpi_),
                          std::min<int>(x, info.rcWork.right - width -
                                               ui::Scale(8, dpi_)));
        y = std::max<int>(info.rcWork.top + ui::Scale(8, dpi_),
                          std::min<int>(y, info.rcWork.bottom - height -
                                               ui::Scale(8, dpi_)));
    }

    const bool sizeChanged = width != lastWidth_ || height != lastHeight_;
    if (x != lastX_ || y != lastY_ || sizeChanged) {
        lastX_ = x;
        lastY_ = y;
        lastWidth_ = width;
        lastHeight_ = height;
        log::Debug("UI", "CAPTION_OVERLAY_GEOMETRY", L"",
                   L"Live caption overlay positioned.",
                  {{"targetLeft", std::to_string(targetRect.left)},
                   {"targetTop", std::to_string(targetRect.top)},
                   {"targetRight", std::to_string(targetRect.right)},
                   {"targetBottom", std::to_string(targetRect.bottom)},
                   {"dpi", std::to_string(dpi_)},
                   {"x", std::to_string(x)},
                   {"y", std::to_string(y)},
                   {"width", std::to_string(width)},
                   {"height", std::to_string(height)},
                   {"lines", std::to_string(latestLines_.size())}});
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    if (sizeChanged) ApplyRegion();
    SetShown(true);
    return true;
}

void CaptionOverlay::ApplyRegion() {
    if (hwnd_ == nullptr) return;
    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1,
                                     ui::Scale(16, dpi_), ui::Scale(16, dpi_));
    if (region != nullptr && SetWindowRgn(hwnd_, region, TRUE) == 0) {
        DeleteObject(region);
    }
}

void CaptionOverlay::ReleaseBackBuffer() {
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

void CaptionOverlay::Paint() {
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
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        const Gdiplus::RectF background(0.0f, 0.0f, static_cast<float>(width),
                                        static_cast<float>(height));
        ui::FillRoundRect(graphics, background, ui::ScaleF(16.0f, dpi_),
                          Gdiplus::Color(255, 18, 22, 29));

        const float side = ui::ScaleF(static_cast<float>(kSideMarginLogical), dpi_);
        float y = ui::ScaleF(static_cast<float>(kPaddingLogical), dpi_);
        Gdiplus::StringFormat format;
        format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        format.SetHotkeyPrefix(Gdiplus::HotkeyPrefixNone);

        if (hasPrevious_ && !previousLine_.empty() && previousFont_ != nullptr) {
            Gdiplus::SolidBrush brush(Gdiplus::Color(190, 199, 208, 222));
            graphics.DrawString(previousLine_.c_str(), -1, previousFont_,
                                Gdiplus::PointF(side, y), &format, &brush);
            y += ui::ScaleF(static_cast<float>(kPreviousLineLogical + kLineGapLogical),
                            dpi_);
        }

        const Gdiplus::Color latestColor =
            latestMicrophone_ ? Gdiplus::Color(255, 176, 236, 192)
                              : Gdiplus::Color(255, 176, 208, 255);
        // A slim source accent bar makes "Them" vs "Me" obvious without
        // stealing width from the text.
        const float barHeight =
            static_cast<float>(latestLines_.size()) *
            ui::ScaleF(static_cast<float>(kLatestLineLogical), dpi_);
        ui::FillRoundRect(
            graphics,
            Gdiplus::RectF(ui::ScaleF(6.0f, dpi_), y, ui::ScaleF(3.0f, dpi_),
                           barHeight),
            ui::ScaleF(1.5f, dpi_), latestColor);

        Gdiplus::SolidBrush brush(latestColor);
        for (const std::wstring& line : latestLines_) {
            graphics.DrawString(line.c_str(), -1, captionFont_,
                                Gdiplus::PointF(side, y), &format, &brush);
            y += ui::ScaleF(static_cast<float>(kLatestLineLogical), dpi_);
        }
    }

    HDC dc = GetDC(hwnd_);
    BitBlt(dc, 0, 0, width, height, memDc_, 0, 0, SRCCOPY);
    ReleaseDC(hwnd_, dc);
}

LRESULT CALLBACK CaptionOverlay::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<CaptionOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self != nullptr ? self->WndProc(hwnd, msg, wp, lp)
                           : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CaptionOverlay::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            BeginPaint(hwnd, &ps);
            Paint();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER:
            if (wp == kOverlayTimerId) {
                if (paused_ || feed_ == nullptr) return 0;
                if (feed_->Sequence() != lastSequence_) {
                    UpdateContent();
                } else {
                    Position();
                }
                return 0;
            }
            break;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_DESTROY:
            KillTimer(hwnd, kOverlayTimerId);
            hwnd_ = nullptr;
            shown_ = false;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace vsr::app
