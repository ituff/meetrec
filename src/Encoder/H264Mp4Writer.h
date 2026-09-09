// Virtual Screen Recorder - H.264 MP4 sink writer (FR-050..FR-058, FR-062,
// FR-063; T031; ADR-004).
//
// Media Foundation sink writer pipeline:
//   NV12 D3D11 texture -> IMFSample -> [H.264 MFT] -> MP4 sink (.partial.mp4)
//
// Hardware path: D3D11-aware MFT + MF_SINK_WRITER_D3D_MANAGER, zero CPU
// copies. Documented fallback: when only a software (non-D3D11-aware) encoder
// exists, frames are staged to system memory. Finalize drains, finalizes,
// closes, and reports; the caller performs the atomic rename to .mp4.
#pragma once

#include <d3d11.h>
#include <filesystem>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <atomic>
#include <memory>

#include <Infrastructure/Error.h>
#include <Infrastructure/Result.h>
#include "EncoderProbe.h"
#include <Capture/GraphicsDevice.h>
#include "VideoPreset.h"

namespace vsr::encoder {

class H264Mp4Writer {
public:
    // Creates the writer for a `.partial.mp4` path. Fails BEFORE creating any
    // file when no compliant encoder can be activated (FR-063).
    static Result<std::unique_ptr<H264Mp4Writer>> Create(const capture::GraphicsDevice& device,
                                                         const EncoderCapability& encoder,
                                                         const PresetParams& preset,
                                                         const std::filesystem::path& partialPath);

    ~H264Mp4Writer();

    H264Mp4Writer(const H264Mp4Writer&) = delete;
    H264Mp4Writer& operator=(const H264Mp4Writer&) = delete;

    // Submits one NV12 frame with 100ns-unit timestamp and duration.
    Result<void> SubmitFrame(ID3D11Texture2D* nv12Frame, std::int64_t time100Ns,
                             std::int64_t duration100Ns);

    // Drains pending samples, finalizes the sink, closes the file. Idempotent
    // (FR-073): a second call reports the first result without side effects.
    Result<void> Finalize();

    bool IsFinalized() const noexcept { return finalized_.load(); }
    std::uint64_t WrittenFrames() const noexcept { return writtenFrames_.load(); }

private:
    H264Mp4Writer() = default;
    Result<void> SubmitFrameGpu(ID3D11Texture2D* nv12Frame, std::int64_t time100Ns,
                                std::int64_t duration100Ns);
    Result<void> SubmitFrameCpu(ID3D11Texture2D* nv12Frame, std::int64_t time100Ns,
                                std::int64_t duration100Ns);
    Result<void> EnsureStaging(ID3D11Texture2D* nv12Frame);
    void CloseWriter() noexcept;

    Microsoft::WRL::ComPtr<IMFSinkWriter> writer_;
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> deviceManager_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
    Microsoft::WRL::ComPtr<IMFMediaBuffer> cpuBuffer_;
    DWORD streamIndex_ = 0;
    EncoderCapability encoder_{};
    PresetParams preset_{};
    std::filesystem::path partialPath_;
    std::int64_t lastTimestamp_ = -1;
    std::atomic<bool> finalized_{false};
    std::atomic<std::uint64_t> writtenFrames_{0};
};

// MF lifecycle helper: balanced MFStartup/MFShutdown per process.
class MFLifetime {
public:
    static Result<void> EnsureStarted();
    static void NotifyShutdown();
};

} // namespace vsr::encoder
