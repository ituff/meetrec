#include "PreviewPanel.h"

#include <algorithm>

#include <Application/Coordinator.h>

#pragma comment(lib, "dwmapi.lib")

namespace vsr::app {

namespace {
constexpr wchar_t kPreviewClassName[] = L"VsrPreviewPanel";
constexpr UINT kGdiHdrSize = sizeof(BITMAPINFOHEADER);
} // namespace

PreviewPanel::~PreviewPanel() {
    Destroy();
}

void PreviewPanel::Create(HWND parent, HINSTANCE instance) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &PreviewPanel::WndProcThunk;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kPreviewClassName;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(0, kPreviewClassName, L"Preview",
                            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 320, 180, parent,
                            nullptr, instance, nullptr);
    if (hwnd_ != nullptr) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    }
}

void PreviewPanel::Destroy() {
    HideThumbnail();
    ReleaseBackBuffer();
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (bitmap_ != nullptr) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }
    frameBgra_.clear();
    frameW_ = 0;
    frameH_ = 0;
}

LRESULT CALLBACK PreviewPanel::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<PreviewPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self != nullptr ? self->WndProc(hwnd, msg, wp, lp)
                           : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT PreviewPanel::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            if (!frameBgra_.empty() && frameW_ > 0 && frameH_ > 0) {
                Render(dc);
            } else {
                RECT rc = {};
                GetClientRect(hwnd, &rc);
                FillRect(dc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            UpdateThumbnailRect();
            return 0;
        case WM_DESTROY:
            hwnd_ = nullptr;
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void PreviewPanel::Clear() {
    HideThumbnail();
    frameBgra_.clear();
    frameW_ = 0;
    frameH_ = 0;
    if (bitmap_ != nullptr) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }
    ReleaseBackBuffer();
    if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
}

bool PreviewPanel::ShowThumbnail(HWND source) {
    if (hwnd_ == nullptr || source == nullptr || !IsWindow(source)) return false;
    HideThumbnail();
    HTHUMBNAIL thumbnail = nullptr;
    if (FAILED(DwmRegisterThumbnail(hwnd_, source, &thumbnail)) || thumbnail == nullptr) {
        return false;
    }
    thumbnail_ = thumbnail;
    thumbnailSource_ = source;
    UpdateThumbnailRect();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void PreviewPanel::UpdateThumbnailRect() {
    if (thumbnail_ == nullptr || hwnd_ == nullptr || thumbnailSource_ == nullptr) return;
    RECT dest = {};
    RECT source = {};
    GetClientRect(hwnd_, &dest);
    GetClientRect(thumbnailSource_, &source);
    const int destW = dest.right - dest.left;
    const int destH = dest.bottom - dest.top;
    const int sourceW = source.right - source.left;
    const int sourceH = source.bottom - source.top;
    if (destW <= 0 || destH <= 0 || sourceW <= 0 || sourceH <= 0) return;

    const double scale = std::min(static_cast<double>(destW) / sourceW,
                                  static_cast<double>(destH) / sourceH);
    const int width = static_cast<int>(sourceW * scale);
    const int height = static_cast<int>(sourceH * scale);
    RECT destination = {dest.left + (destW - width) / 2,
                        dest.top + (destH - height) / 2,
                        dest.left + (destW - width) / 2 + width,
                        dest.top + (destH - height) / 2 + height};

    DWM_THUMBNAIL_PROPERTIES properties = {};
    properties.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE |
                         DWM_TNP_OPACITY | DWM_TNP_VISIBLE | DWM_TNP_SOURCECLIENTAREAONLY;
    properties.rcDestination = destination;
    properties.rcSource = source;
    properties.opacity = 255;
    properties.fVisible = TRUE;
    properties.fSourceClientAreaOnly = TRUE;
    DwmUpdateThumbnailProperties(thumbnail_, &properties);
}

void PreviewPanel::HideThumbnail() {
    if (thumbnail_ != nullptr) {
        DwmUnregisterThumbnail(thumbnail_);
        thumbnail_ = nullptr;
    }
    thumbnailSource_ = nullptr;
    if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
}

void PreviewPanel::SetStaticFrame(std::vector<std::uint8_t> bgra, UINT width, UINT height) {
    if (width == 0 || height == 0 || bgra.size() < static_cast<size_t>(width) * height * 4) {
        return;
    }
    HideThumbnail();
    frameBgra_ = std::move(bgra);
    frameW_ = width;
    frameH_ = height;
    Render();
}

bool PreviewPanel::CopyLastFrame(std::vector<std::uint8_t>& outBgra, UINT& width,
                                 UINT& height) const {
    if (frameBgra_.empty() || frameW_ == 0 || frameH_ == 0) return false;
    outBgra = frameBgra_;
    width = frameW_;
    height = frameH_;
    return true;
}

void PreviewPanel::Tick() {
    if (hwnd_ == nullptr || !enabled_) return;
    // Sample at ~10 FPS, independent of the recording frame rate
    // (design.md §4.9).
    const DWORD now = GetTickCount();
    if (now - lastRenderMs_ < 100) return;
    lastRenderMs_ = now;

    UINT w = 0;
    UINT h = 0;
    std::vector<std::uint8_t> bgra;
    if (coordinator_.TryGetPreviewFrameCpu(bgra, w, h) && w > 0 && h > 0) {
        // Live capture replaces the idle DWM thumbnail.
        HideThumbnail();
        frameBgra_ = std::move(bgra);
        frameW_ = w;
        frameH_ = h;
        Render();
    }
}

void PreviewPanel::ReleaseBackBuffer() {
    if (backDc_ != nullptr) {
        DeleteDC(backDc_);
        backDc_ = nullptr;
    }
    if (backBmp_ != nullptr) {
        DeleteObject(backBmp_);
        backBmp_ = nullptr;
    }
    backW_ = 0;
    backH_ = 0;
}

void PreviewPanel::Render(HDC targetDc) {
    if (hwnd_ == nullptr || frameBgra_.empty() || frameW_ == 0 || frameH_ == 0) return;

    // Reuse the DIB section across frames of the same size (10 FPS ticks
    // would otherwise churn an ~8 MB GDI allocation per frame).
    if (bitmap_ == nullptr || dibW_ != frameW_ || dibH_ != frameH_) {
        if (bitmap_ != nullptr) {
            DeleteObject(bitmap_);
            bitmap_ = nullptr;
        }
        BITMAPINFOHEADER header = {};
        header.biSize = sizeof(BITMAPINFOHEADER);
        header.biWidth = static_cast<LONG>(frameW_);
        header.biHeight = -static_cast<LONG>(frameH_); // top-down
        header.biPlanes = 1;
        header.biBitCount = 32;
        header.biCompression = BI_RGB;

        void* bits = nullptr;
        bitmap_ = CreateDIBSection(nullptr, reinterpret_cast<const BITMAPINFO*>(&header),
                                   DIB_RGB_COLORS, &bits, nullptr, 0);
        dibBits_ = bits;
        dibW_ = frameW_;
        dibH_ = frameH_;
        if (bitmap_ == nullptr || bits == nullptr) {
            dibBits_ = nullptr;
            return;
        }
    }
    if (dibBits_ == nullptr) return;
    memcpy(dibBits_, frameBgra_.data(), frameBgra_.size());

    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    const int dstW = rc.right - rc.left;
    const int dstH = rc.bottom - rc.top;
    if (dstW <= 0 || dstH <= 0) return;

    if (backDc_ == nullptr || backW_ != dstW || backH_ != dstH) {
        ReleaseBackBuffer();
        HDC screen = GetDC(nullptr);
        backDc_ = CreateCompatibleDC(screen);
        backBmp_ = CreateCompatibleBitmap(screen, dstW, dstH);
        ReleaseDC(nullptr, screen);
        if (backDc_ == nullptr || backBmp_ == nullptr) {
            ReleaseBackBuffer();
            return;
        }
        SelectObject(backDc_, backBmp_);
        backW_ = dstW;
        backH_ = dstH;
    }

    // Compose the whole panel offscreen: black letterbox plus the scaled
    // frame. A single BitBlt follows, so the black background is never shown
    // between the erase and the image (the old 10 FPS flicker).
    RECT full = {0, 0, dstW, dstH};
    FillRect(backDc_, &full, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    HDC sourceDc = CreateCompatibleDC(backDc_);
    if (sourceDc == nullptr) return;
    HGDIOBJ old = SelectObject(sourceDc, bitmap_);
    SetStretchBltMode(backDc_, HALFTONE);
    SetBrushOrgEx(backDc_, 0, 0, nullptr);
    const double scale = std::min(static_cast<double>(dstW) / frameW_,
                                  static_cast<double>(dstH) / frameH_);
    const int drawW = static_cast<int>(frameW_ * scale);
    const int drawH = static_cast<int>(frameH_ * scale);
    const int offX = (dstW - drawW) / 2;
    const int offY = (dstH - drawH) / 2;
    StretchBlt(backDc_, offX, offY, drawW, drawH, sourceDc, 0, 0,
               static_cast<int>(frameW_), static_cast<int>(frameH_), SRCCOPY);
    SelectObject(sourceDc, old);
    DeleteDC(sourceDc);

    HDC windowDc = targetDc != nullptr ? targetDc : GetDC(hwnd_);
    BitBlt(windowDc, 0, 0, dstW, dstH, backDc_, 0, 0, SRCCOPY);
    if (targetDc == nullptr) ReleaseDC(hwnd_, windowDc);
}

} // namespace vsr::app
