// Virtual Screen Recorder - GDI window capture session (PrintWindow backend).
//
// Windows.Graphics.Capture window capture does not deliver real content in
// some environments (observed on OS build 26200 inside an RDP session: a
// single blank frame, never updates). This backend renders the target window
// into a DIB via PrintWindow(PW_CLIENTONLY | PW_RENDERFULLCONTENT) on a fixed
// rate timer and uploads the BGRA bits straight into a D3D texture, feeding
// the same bounded queue as the WGC session. PW_RENDERFULLCONTENT captures
// DirectComposition/Chromium content (Win 8.1+).
#pragma once

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>
#include <windows.h>

#include <Infrastructure/Clock.h>
#include <Infrastructure/Error.h>
#include <Infrastructure/Result.h>
#include "BoundedFrameQueue.h"
#include "CaptureSession.h"
#include "GraphicsDevice.h"

namespace vsr::capture {

class GdiCaptureSession final : public ICaptureSession {
public:
    // Captures `window`'s client area at ~`fps` frames per second.
    static Result<std::unique_ptr<GdiCaptureSession>> Start(const GraphicsDevice& device,
                                                            HWND window, std::uint32_t fps,
                                                            BoundedFrameQueue& queue,
                                                            IClock& clock);

    ~GdiCaptureSession() override;

    GdiCaptureSession(const GdiCaptureSession&) = delete;
    GdiCaptureSession& operator=(const GdiCaptureSession&) = delete;

    void Stop() noexcept override;
    FramePoolStats Stats() const noexcept override;
    void SetCursorCaptureEnabled(bool enabled) noexcept override;
    bool IsCursorCaptureEnabled() const noexcept override { return cursorCapture_.load(); }

    // Preview tap: copies the most recent frame into `destination` (a BGRA
    // texture of at least the captured size). Returns false when idle.
    bool TryGetLatestFrame(ID3D11Texture2D* destination) override;
    bool CopyLatestToCpu(std::vector<std::uint8_t>& outBgra, UINT& width, UINT& height) override;

private:
    GdiCaptureSession(const GraphicsDevice& device, HWND window, BoundedFrameQueue& queue,
                      IClock& clock);
    void CaptureLoop(std::uint32_t intervalMs);

    const GraphicsDevice& device_;
    HWND window_ = nullptr;
    BoundedFrameQueue& queue_;
    IClock& clock_;

    std::thread thread_;
    std::atomic<bool> stopped_{false};
    std::atomic<bool> cursorCapture_{false};

    // Latest frame, kept as CPU bits (the GDI source is CPU memory anyway).
    std::mutex latestMutex_;
    std::vector<std::uint8_t> latestBits_;
    UINT latestWidth_ = 0;
    UINT latestHeight_ = 0;

    std::atomic<std::uint64_t> captured_{0};
    std::atomic<std::uint32_t> lastWidth_{0};
    std::atomic<std::uint32_t> lastHeight_{0};
};

} // namespace vsr::capture