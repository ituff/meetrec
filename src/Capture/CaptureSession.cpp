#include "CaptureSession.h"
#include "CaptureInterop.h"

#include <cstring>

#include <Infrastructure/Log.h>

#include <winrt/Windows.Graphics.DirectX.h>

namespace vsr::capture {

using winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;

namespace {

// Queue capacity + slack: one texture per possibly-queued frame plus the one
// being filled and the one referenced by the preview tap.
constexpr std::size_t kQueueCapacity = 3;
constexpr std::size_t kPoolSlack = 2;

} // namespace

CaptureSession::CaptureSession(const GraphicsDevice& device,
                                             BoundedFrameQueue& queue, IClock& clock)
    : device_(device), queue_(queue), clock_(clock), pool_(std::make_shared<PoolState>()) {}

CaptureSession::~CaptureSession() {
    Stop();
}


// Shared tail of both Start overloads: pool creation, event wiring, session
// start. `targetLabel` only feeds the structured log line.
Result<std::unique_ptr<CaptureSession>> CaptureSession::StartInternal(
    const GraphicsDevice& device, const GraphicsCaptureItem item, BoundedFrameQueue& queue,
    IClock& clock, const wchar_t* targetLabel) {
    auto winrtDeviceResult = CreateWinrtDevice(device.Device());
    if (!winrtDeviceResult) return winrtDeviceResult.TakeError();

    auto* session = new CaptureSession(device, queue, clock);
    session->winrtDevice_ = winrtDeviceResult.Value();
    session->item_ = item;
    {
        std::lock_guard<std::mutex> lock(session->pool_->mutex);
        session->pool_->width = static_cast<UINT>(item.Size().Width);
        session->pool_->height = static_cast<UINT>(item.Size().Height);
        session->pool_->textures.resize(kQueueCapacity + kPoolSlack);
        session->pool_->inFlight.assign(kQueueCapacity + kPoolSlack, false);
    }

    try {
        session->framePool_ = Direct3D11CaptureFramePool::CreateFreeThreaded(
            session->winrtDevice_, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, item.Size());
        session->frameArrivedRevoker_ = session->framePool_.FrameArrived(
            winrt::auto_revoke, {session, &CaptureSession::OnFrameArrived});
        session->session_ = session->framePool_.CreateCaptureSession(item);
        // Cursor capture default off (FR-049); ignore when the API is absent.
        try {
            session->session_.IsCursorCaptureEnabled(false);
            session->cursorCapture_.store(true);
        } catch (...) {
            session->cursorCapture_.store(false);
        }
        // Best effort: drop the yellow capture border on builds that expose the
        // toggle (unpackaged apps may be denied; the border is cosmetic only).
        try {
            session->session_.IsBorderRequired(false);
        } catch (...) {
        }
        session->session_.StartCapture();
    } catch (const winrt::hresult_error& e) {
        delete session;
        return ErrorFromHresult(static_cast<HRESULT>(e.code().value), ErrorCode::CaptureInitFailed,
                                L"Capture could not start on the selected target.",
                                L"WinRT capture start failed: " + std::wstring(e.message().c_str()),
                                true);
    } catch (...) {
        delete session;
        return Error(ErrorCode::CaptureInitFailed,
                     L"Capture could not start on the selected target.",
                     L"Unknown error starting capture", E_FAIL, true);
    }

    UINT w = 0, h = 0;
    {
        std::lock_guard<std::mutex> lock(session->pool_->mutex);
        w = session->pool_->width;
        h = session->pool_->height;
    }
    log::Info("Capture", "CAPTURE_STARTED", L"", targetLabel,
              {{"width", std::to_string(w)},
               {"height", std::to_string(h)},
               {"format", "B8G8R8A8"}});
    return std::unique_ptr<CaptureSession>(session);
}


Result<std::unique_ptr<CaptureSession>> CaptureSession::Start(
    const GraphicsDevice& device, HMONITOR monitor, BoundedFrameQueue& queue, IClock& clock) {
    auto itemResult = CreateCaptureItemForMonitor(monitor);
    if (!itemResult) return itemResult.TakeError();
    return StartInternal(device, itemResult.Value(), queue, clock,
                         L"Monitor capture session started.");
}

Result<std::unique_ptr<CaptureSession>> CaptureSession::Start(
    const GraphicsDevice& device, HWND window, BoundedFrameQueue& queue, IClock& clock) {
    auto itemResult = CreateCaptureItemForWindow(window);
    if (!itemResult) return itemResult.TakeError();
    return StartInternal(device, itemResult.Value(), queue, clock,
                         L"Window capture session started.");
}

void CaptureSession::Stop() noexcept {
    bool expected = false;
    if (!stopped_.compare_exchange_strong(expected, true)) return;
    try {
        if (frameArrivedRevoker_) frameArrivedRevoker_.revoke();
        if (session_) session_.Close();
        if (framePool_) framePool_.Close();
    } catch (...) {
        // NFR-011: never let exceptions escape a destructor path.
    }
    session_ = nullptr;
    framePool_ = nullptr;
    item_ = nullptr;
    winrtDevice_ = nullptr;
    if (pool_) {
        std::lock_guard<std::mutex> lock(pool_->mutex);
        pool_->textures.clear();
    }
    std::lock_guard<std::mutex> lock(latestMutex_);
    latestTexture_.Reset();
    log::Info("Capture", "CAPTURE_STOPPED", L"", L"Monitor capture session stopped.");
}

FramePoolStats CaptureSession::Stats() const noexcept {
    FramePoolStats s;
    const auto counters = queue_.Counters();
    s.captured = counters.captured;
    s.dropped = counters.dropped + droppedByPool_.load(std::memory_order_relaxed);
    s.lastWidth = lastWidth_.load(std::memory_order_relaxed);
    s.lastHeight = lastHeight_.load(std::memory_order_relaxed);
    return s;
}

void CaptureSession::SetCursorCaptureEnabled(bool enabled) noexcept {
    cursorCapture_.store(enabled);
    try {
        if (session_) session_.IsCursorCaptureEnabled(enabled);
    } catch (...) {
        // Best effort on OS builds without the API (FR-049 is SHOULD).
    }
}

bool CaptureSession::AcquirePooledTexture(UINT width, UINT height, ID3D11Texture2D** out,
                                                 std::size_t* slot) {
    std::lock_guard<std::mutex> lock(pool_->mutex);
    if (pool_->width != width || pool_->height != height) {
        // Content-size change: recreate pool textures at the new size. Frames
        // still in flight hold their own COM references, so they stay valid.
        pool_->textures.clear();
        pool_->textures.resize(kQueueCapacity + kPoolSlack);
        std::fill(pool_->inFlight.begin(), pool_->inFlight.end(), false);
        pool_->next = 0;
        pool_->width = width;
        pool_->height = height;
    }
    for (std::size_t i = 0; i < pool_->textures.size(); ++i) {
        const std::size_t idx = (pool_->next + i) % pool_->textures.size();
        if (pool_->inFlight[idx]) continue;
        if (pool_->textures[idx] == nullptr) {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            HRESULT hr =
                device_.Device()->CreateTexture2D(&desc, nullptr, pool_->textures[idx].GetAddressOf());
            if (FAILED(hr)) return false;
        }
        pool_->inFlight[idx] = true;
        pool_->next = (idx + 1) % pool_->textures.size();
        *out = pool_->textures[idx].Get();
        *slot = idx;
        return true;
    }
    return false; // all pooled textures outstanding -> drop this frame
}

void CaptureSession::OnFrameArrived(Direct3D11CaptureFramePool const& sender,
                                           winrt::Windows::Foundation::IInspectable const&) {
    if (stopped_.load(std::memory_order_acquire)) return;

    // Minimal work: extract texture, copy into pooled texture, enqueue.
    Direct3D11CaptureFrame frame = nullptr;
    try {
        frame = sender.TryGetNextFrame();
    } catch (...) {
        return;
    }
    if (frame == nullptr) return;

    const auto srcTexture = GetTextureFromSurface(frame.Surface());
    if (srcTexture == nullptr) return;

    const UINT width = static_cast<UINT>(frame.ContentSize().Width);
    const UINT height = static_cast<UINT>(frame.ContentSize().Height);
    lastWidth_.store(width, std::memory_order_relaxed);
    lastHeight_.store(height, std::memory_order_relaxed);

    ID3D11Texture2D* pooled = nullptr;
    std::size_t slot = 0;
    if (!AcquirePooledTexture(width, height, &pooled, &slot)) {
        droppedByPool_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    D3D11_TEXTURE2D_DESC srcDesc = {};
    srcTexture->GetDesc(&srcDesc);
    D3D11_BOX box = {};
    box.right = std::min<UINT>(width, srcDesc.Width);
    box.bottom = std::min<UINT>(height, srcDesc.Height);
    device_.Context()->CopySubresourceRegion(pooled, 0, 0, 0, 0, srcTexture.Get(), 0, &box);
    device_.Context()->Flush();

    // Payload owns its own COM reference so the frame stays valid even if the
    // pool recreates textures while the frame waits in the queue (ADR-005).
    pooled->AddRef();
    auto poolState = pool_;
    CapturedFrame captured;
    captured.payload = std::shared_ptr<void>(pooled, [poolState, slot](void* p) {
        auto* texture = static_cast<ID3D11Texture2D*>(p);
        texture->Release();
        if (poolState) {
            std::lock_guard<std::mutex> lock(poolState->mutex);
            if (slot < poolState->inFlight.size()) poolState->inFlight[slot] = false;
        }
    });
    captured.timestampUs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(clock_.MonotonicNow()).count());
    captured.width = width;
    captured.height = height;

    {
        std::lock_guard<std::mutex> lock(latestMutex_);
        latestTexture_ = pooled; // ComPtr::operator= takes its own reference
    }

    queue_.Push(std::move(captured));
}

bool CaptureSession::TryGetLatestFrame(ID3D11Texture2D* destination) {
    if (destination == nullptr) return false;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> latest;
    {
        std::lock_guard<std::mutex> lock(latestMutex_);
        latest = latestTexture_;
    }
    if (latest == nullptr) return false;
    D3D11_TEXTURE2D_DESC latestDesc = {};
    latest->GetDesc(&latestDesc);
    D3D11_TEXTURE2D_DESC dstDesc = {};
    destination->GetDesc(&dstDesc);
    if (dstDesc.Width < latestDesc.Width || dstDesc.Height < latestDesc.Height) return false;
    D3D11_BOX box = {};
    box.right = latestDesc.Width;
    box.bottom = latestDesc.Height;
    device_.Context()->CopySubresourceRegion(destination, 0, 0, 0, 0, latest.Get(), 0, &box);
    return true;
}

bool CaptureSession::CopyLatestToCpu(std::vector<std::uint8_t>& outBgra, UINT& width,
                                            UINT& height) {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> latest;
    {
        std::lock_guard<std::mutex> lock(latestMutex_);
        latest = latestTexture_;
    }
    if (latest == nullptr) return false;

    D3D11_TEXTURE2D_DESC latestDesc = {};
    latest->GetDesc(&latestDesc);

    D3D11_TEXTURE2D_DESC stagingDesc = {};
    if (previewStaging_ != nullptr) previewStaging_->GetDesc(&stagingDesc);
    if (previewStaging_ == nullptr || stagingDesc.Width != latestDesc.Width ||
        stagingDesc.Height != latestDesc.Height) {
        previewStaging_.Reset();
        D3D11_TEXTURE2D_DESC desc = latestDesc;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        HRESULT hr =
            device_.Device()->CreateTexture2D(&desc, nullptr, previewStaging_.GetAddressOf());
        if (FAILED(hr)) return false;
    }

    device_.Context()->CopyResource(previewStaging_.Get(), latest.Get());

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr =
        device_.Context()->Map(previewStaging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    width = latestDesc.Width;
    height = latestDesc.Height;
    outBgra.resize(static_cast<size_t>(width) * height * 4);
    const auto* src = static_cast<const std::uint8_t*>(mapped.pData);
    for (UINT row = 0; row < height; ++row) {
        memcpy(outBgra.data() + static_cast<size_t>(row) * width * 4,
               src + static_cast<size_t>(row) * mapped.RowPitch, static_cast<size_t>(width) * 4);
    }
    device_.Context()->Unmap(previewStaging_.Get(), 0);
    return true;
}

} // namespace vsr::capture
