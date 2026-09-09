#include "PreviewPanel.h"

#include <Application/Coordinator.h>

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
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kPreviewClassName;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(WS_EX_STATICEDGE, kPreviewClassName, L"Preview",
                            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 320, 180, parent,
                            nullptr, instance, nullptr);
    if (hwnd_ != nullptr) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    }
}

void PreviewPanel::Destroy() {
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
            RECT rc = {};
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            hwnd_ = nullptr;
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void PreviewPanel::Clear() {
    frameBgra_.clear();
    frameW_ = 0;
    frameH_ = 0;
    if (bitmap_ != nullptr) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }
    if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, TRUE);
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
        frameBgra_ = std::move(bgra);
        frameW_ = w;
        frameH_ = h;
        Render();
    }
}

void PreviewPanel::Render() {
    if (hwnd_ == nullptr || frameBgra_.empty() || frameW_ == 0 || frameH_ == 0) return;

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
    bitmap_ = CreateDIBSection(nullptr, reinterpret_cast<const BITMAPINFO*>(&header), DIB_RGB_COLORS,
                               &bits, nullptr, 0);
    if (bitmap_ == nullptr || bits == nullptr) return;
    memcpy(bits, frameBgra_.data(), frameBgra_.size());

    HDC windowDc = GetDC(hwnd_);
    HDC memDc = CreateCompatibleDC(windowDc);
    HGDIOBJ old = SelectObject(memDc, bitmap_);

    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    const LONG dstW = rc.right - rc.left;
    const LONG dstH = rc.bottom - rc.top;
    SetStretchBltMode(windowDc, COLORONCOLOR);
    // Preserve aspect ratio within the panel.
    const double scale = std::min(static_cast<double>(dstW) / frameW_,
                                  static_cast<double>(dstH) / frameH_);
    const LONG drawW = static_cast<LONG>(frameW_ * scale);
    const LONG drawH = static_cast<LONG>(frameH_ * scale);
    const LONG offX = (dstW - drawW) / 2;
    const LONG offY = (dstH - drawH) / 2;
    StretchBlt(windowDc, offX, offY, drawW, drawH, memDc, 0, 0, static_cast<LONG>(frameW_),
               static_cast<LONG>(frameH_), SRCCOPY);

    SelectObject(memDc, old);
    DeleteDC(memDc);
    ReleaseDC(hwnd_, windowDc);
}

} // namespace vsr::app
