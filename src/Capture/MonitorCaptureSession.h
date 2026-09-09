// Virtual Screen Recorder - virtual monitor capture session (FR-040..FR-048,
// T022, T023; ADR-003, ADR-005).
//
// Owns the GraphicsCaptureItem and free-threaded Direct3D11CaptureFramePool
// for the virtual monitor. The FrameArrived callback does the minimum work:
// copy the pool surface into a pooled BGRA texture (proving texture ownership
// per ADR-005) and push it onto the bounded queue. Disk I/O, UI, and encoder
// work are strictly forbidden here.
//
// The session holds no HWND dependency, so capture continues while the
// controller is minimized, hidden, or in the tray (FR-047). Closing the
// preview cannot terminate capture (FR-048) because preview only samples the
// most recent pooled texture.
#pragma once

#include <algorithm>
#include <atomic>
#include <d3d11.h>
#include <memory>
#include <mutex>
#include <vector>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <wrl/client.h>

#include <Infrastructure/Clock.h>
#include <Infrastructure/Error.h>
#include <Infrastructure/Result.h>
#include "BoundedFrameQueue.h"
#include "GraphicsDevice.h"

namespace vsr::capture {

struct FramePoolStats {
    std::uint64_t captured = 0;
    std::uint64_t dropped = 0;
    std::uint32_t lastWidth = 0;
    std::uint32_t lastHeight = 0;
};

class MonitorCaptureSession {
public:
    // Starts capture of `monitor` at the display's current content size.
    // Queue-capacity + 2 pooled BGRA textures are preallocated on demand.
    static Result<std::unique_ptr<MonitorCaptureSession>> Start(const GraphicsDevice& device,
                                                                HMONITOR monitor,
                                                                BoundedFrameQueue& queue,
                                                                IClock& clock);

    ~MonitorCaptureSession();

    MonitorCaptureSession(const MonitorCaptureSession&) = delete;
    MonitorCaptureSession& operator=(const MonitorCaptureSession&) = delete;

    // Stops the capture session. Idempotent and safe to call from any thread.
    // The caller must keep the session alive until the queue is fully drained.
    void Stop() noexcept;

    // FR-042: verified frame size and counters of the session.
    FramePoolStats Stats() const noexcept;

    // Cursor capture toggle (FR-049); best effort — older OS builds ignore it.
    void SetCursorCaptureEnabled(bool enabled) noexcept;
    bool IsCursorCaptureEnabled() const noexcept { return cursorCapture_.load(); }

    // Preview tap (FR-048): copies the most recent captured frame into
    // `destination` (a BGRA texture of at least the captured size). Returns
    // false when no frame is available yet. Never blocks recording.
    bool TryGetLatestFrame(ID3D11Texture2D* destination);

    // Preview tap for the UI: reads the latest frame into a BGRA byte buffer
    // (top-down). CPU copy happens ONLY here, at preview rate, never in the
    // recording path (design.md §4.9). Returns false when idle.
    bool CopyLatestToCpu(std::vector<std::uint8_t>& outBgra, UINT& width, UINT& height);

private:
    MonitorCaptureSession(const GraphicsDevice& device, BoundedFrameQueue& queue, IClock& clock);

    void OnFrameArrived(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const&
                            sender,
                        winrt::Windows::Foundation::IInspectable const&);

    // Shared between the session and every outstanding frame payload so that
    // a consumed frame's slot-release can never touch a destroyed session.
    struct PoolState {
        std::mutex mutex;
        std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>> textures;
        std::vector<bool> inFlight;
        std::size_t next = 0;
        UINT width = 0;
        UINT height = 0;
    };

    bool AcquirePooledTexture(UINT width, UINT height, ID3D11Texture2D** out, std::size_t* slot);

    const GraphicsDevice& device_;
    BoundedFrameQueue& queue_;
    IClock& clock_;
    std::shared_ptr<PoolState> pool_;

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item_ = nullptr;
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool framePool_ = nullptr;
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_ = nullptr;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrtDevice_ = nullptr;
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::FrameArrived_revoker
        frameArrivedRevoker_;

    std::mutex latestMutex_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> latestTexture_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> previewStaging_;

    std::atomic<bool> stopped_{false};
    std::atomic<bool> cursorCapture_{false};
    std::atomic<std::uint64_t> droppedByPool_{0};
    std::atomic<std::uint32_t> lastWidth_{0};
    std::atomic<std::uint32_t> lastHeight_{0};
};

} // namespace vsr::capture
