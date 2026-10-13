#include "GdiCaptureSession.h"

#include <d3d11.h>
#include <Infrastructure/Log.h>
#include <Infrastructure/Strings.h>

#include <chrono>

namespace vsr::capture {

GdiCaptureSession::GdiCaptureSession(const GraphicsDevice& device, HWND window,
                                     BoundedFrameQueue& queue, IClock& clock)
    : device_(device), window_(window), queue_(queue), clock_(clock) {}

GdiCaptureSession::~GdiCaptureSession() {
    Stop();
}

Result<std::unique_ptr<GdiCaptureSession>> GdiCaptureSession::Start(
    const GraphicsDevice& device, HWND window, std::uint32_t fps, BoundedFrameQueue& queue,
    IClock& clock) {
    if (window == nullptr || !IsWindow(window)) {
        return Error(ErrorCode::CaptureInitFailed,
                     L"The selected window is no longer available for capture.",
                     L"GdiCaptureSession::Start called with an invalid hwnd", E_POINTER, true);
    }
    auto* session = new GdiCaptureSession(device, window, queue, clock);
    const std::uint32_t fpsClamped = fps == 0 ? 30 : fps;
    session->thread_ = std::thread([session, fpsClamped] { session->CaptureLoop(1000 / fpsClamped); });
    log::Info("Capture", "CAPTURE_STARTED", L"", L"GDI window capture session started.",
              {{"fps", std::to_string(fpsClamped)}});
    return std::unique_ptr<GdiCaptureSession>(session);
}

void GdiCaptureSession::Stop() noexcept {
    bool expected = false;
    if (!stopped_.compare_exchange_strong(expected, true)) return;
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(latestMutex_);
    latestBits_.clear();
    latestWidth_ = 0;
    latestHeight_ = 0;
    log::Info("Capture", "CAPTURE_STOPPED", L"", L"GDI window capture session stopped.");
}

FramePoolStats GdiCaptureSession::Stats() const noexcept {
    FramePoolStats s;
    const auto counters = queue_.Counters();
    s.captured = counters.captured;
    s.dropped = counters.dropped;
    s.lastWidth = lastWidth_.load(std::memory_order_relaxed);
    s.lastHeight = lastHeight_.load(std::memory_order_relaxed);
    return s;
}

void GdiCaptureSession::SetCursorCaptureEnabled(bool enabled) noexcept {
    // PrintWindow cannot synthesize the cursor; the flag is accepted so the
    // shared pipeline stays uniform.
    cursorCapture_.store(enabled);
}

bool GdiCaptureSession::TryGetLatestFrame(ID3D11Texture2D* destination) {
    std::vector<std::uint8_t> bits;
    UINT w = 0;
    UINT h = 0;
    {
        std::lock_guard<std::mutex> lock(latestMutex_);
        if (latestBits_.empty() || destination == nullptr) return false;
        bits = latestBits_;
        w = latestWidth_;
        h = latestHeight_;
    }
    ID3D11DeviceContext* ctx = nullptr;
    device_.Device()->GetImmediateContext(&ctx);
    if (ctx == nullptr) return false;
    D3D11_BOX box = {};
    box.right = w;
    box.bottom = h;
    ctx->UpdateSubresource(destination, 0, &box, bits.data(), w * 4, 0);
    ctx->Release();
    return true;
}

bool GdiCaptureSession::CopyLatestToCpu(std::vector<std::uint8_t>& outBgra, UINT& width,
                                        UINT& height) {
    std::lock_guard<std::mutex> lock(latestMutex_);
    if (latestBits_.empty()) return false;
    outBgra = latestBits_;
    width = latestWidth_;
    height = latestHeight_;
    return true;
}

void GdiCaptureSession::CaptureLoop(std::uint32_t intervalMs) {
    HDC windowDc = GetDC(window_);
    if (windowDc == nullptr) return;

    // Client-area size tracked per frame; the DIB is recreated on change.
    UINT dibW = 0;
    UINT dibH = 0;
    HDC memDc = nullptr;
    HBITMAP dib = nullptr;
    BYTE* bits = nullptr;

    const auto nextWake = [&] {
        return std::chrono::steady_clock::now() + std::chrono::milliseconds(intervalMs);
    };
    auto wake = nextWake();

    while (!stopped_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_until(wake);
        wake = nextWake();
        if (stopped_.load(std::memory_order_acquire)) break;

        RECT rc = {};
        if (!GetClientRect(window_, &rc)) break;
        const UINT w = static_cast<UINT>(rc.right - rc.left);
        const UINT h = static_cast<UINT>(rc.bottom - rc.top);
        if (w == 0 || h == 0) continue;

        if (w != dibW || h != dibH || memDc == nullptr) {
            if (memDc != nullptr) {
                SelectObject(memDc, nullptr);
                DeleteObject(dib);
                DeleteDC(memDc);
                memDc = nullptr;
            }
            BITMAPINFOHEADER bi = {};
            bi.biSize = sizeof(bi);
            bi.biWidth = static_cast<LONG>(w);
            bi.biHeight = -static_cast<LONG>(h); // top-down
            bi.biPlanes = 1;
            bi.biBitCount = 32;
            bi.biCompression = BI_RGB;
            void* bitsPtr = nullptr;
            dib = CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS,
                                   &bitsPtr, nullptr, 0);
            if (dib == nullptr) continue;
            memDc = CreateCompatibleDC(windowDc);
            SelectObject(memDc, dib);
            bits = static_cast<BYTE*>(bitsPtr);
            dibW = w;
            dibH = h;
        }

        // PW_CLIENTONLY (1) | PW_RENDERFULLCONTENT (2): client area including
        // DirectComposition content (Chromium, hardware-accelerated apps).
        if (!PrintWindow(window_, memDc, 1 | 2)) continue;

        const std::size_t byteCount = static_cast<std::size_t>(w) * h * 4;

        // Upload to a D3D texture and enqueue.
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        // VideoProcessorInputView (the converter stage) requires the
        // render-target bind flag; WGC pool textures carry it too.
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        ID3D11Texture2D* texture = nullptr;
        if (FAILED(device_.Device()->CreateTexture2D(&desc, nullptr, &texture)) ||
            texture == nullptr) {
            continue;
        }
        {
            ID3D11DeviceContext* ctx = nullptr;
            device_.Device()->GetImmediateContext(&ctx);
            if (ctx != nullptr) {
                ctx->UpdateSubresource(texture, 0, nullptr, bits, w * 4, 0);
                device_.Context()->Flush();
                ctx->Release();
            }
        }

        CapturedFrame captured;
        captured.payload = std::shared_ptr<void>(texture, [](void* p) {
            static_cast<ID3D11Texture2D*>(p)->Release();
        });
        captured.timestampUs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(clock_.MonotonicNow()).count());
        captured.width = w;
        captured.height = h;
        if (queue_.Push(std::move(captured))) {
            captured_.fetch_add(1, std::memory_order_relaxed);
        }
        lastWidth_.store(w, std::memory_order_relaxed);
        lastHeight_.store(h, std::memory_order_relaxed);

        std::lock_guard<std::mutex> lock(latestMutex_);
        latestBits_.assign(bits, bits + byteCount);
        latestWidth_ = w;
        latestHeight_ = h;
    }

    if (memDc != nullptr) {
        SelectObject(memDc, nullptr);
        DeleteObject(dib);
        DeleteDC(memDc);
    }
    ReleaseDC(window_, windowDc);
}

} // namespace vsr::capture