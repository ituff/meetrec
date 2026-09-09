// Virtual Screen Recorder - H.264 MP4 sink writer (FR-050..FR-058, FR-062,
// FR-063; T031; ADR-004).
//
// Media Foundation sink writer pipeline:
//   NV12 frame -> IMFSample -> [H.264 MFT] -> MP4 sink (.partial.mp4)
//
// Two submission paths, selected by a bounded capability probe at creation:
//   1. GPU path (preferred, zero CPU copies): D3D11-aware hardware MFT +
//      MF_SINK_WRITER_D3D_MANAGER; the NV12 texture is wrapped with
//      MFCreateDXGISurfaceBuffer.
//   2. CPU staging fallback: system-memory NV12 buffers. Used when only a
//      software encoder exists OR when the GPU sample submission is rejected
//      by the driver/MFT combination (observed on some AMD iGPU drivers).
// The probe submits one black frame and flushes; on failure the partial file
// is deleted and the next path is attempted — all before recording starts, so
// NFR-005 stays bounded.
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
    // usable recording when no compliant encoder path can be activated
    // (FR-063). The chosen path (GPU/CPU) is logged as ENCODER_PATH.
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
    bool UsingGpuSamples() const noexcept { return useGpuSamples_; }

    // Watchdog hook: reports EncoderWriteFailed when the sink's queued-byte
    // watermark shows the encoder MFT has stopped consuming (observed as a
    // real driver failure mode; WriteSample itself would otherwise block
    // forever — NFR-007/NFR-014 defense). Cheap; call on a slow cadence.
    Result<void> QueueHealth();

private:
    H264Mp4Writer() = default;

    // Builds a configured writer (no probe). Returns the writer; on failure
    // the partial file has been removed when it was already created.
    static Result<std::unique_ptr<H264Mp4Writer>>
    AttemptCreate(const capture::GraphicsDevice& device, const EncoderCapability& encoder,
                  const PresetParams& preset, const std::filesystem::path& partialPath,
                  bool gpuSamples, const wchar_t** failStep, HRESULT* failHr);

    Result<void> ProbeSubmission(const capture::GraphicsDevice& device, const PresetParams& preset);
    Result<void> SubmitFrameGpu(ID3D11Texture2D* nv12Frame, std::int64_t time100Ns,
                                std::int64_t duration100Ns);
    Result<void> SubmitFrameCpu(ID3D11Texture2D* nv12Frame, std::int64_t time100Ns,
                                std::int64_t duration100Ns);
    Result<void> EnsureStaging(ID3D11Texture2D* nv12Frame);
    void CloseWriter() noexcept;

    Microsoft::WRL::ComPtr<IMFSinkWriter> writer_;
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> deviceManager_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
    DWORD streamIndex_ = 0;
    EncoderCapability encoder_{};
    PresetParams preset_{};
    std::filesystem::path partialPath_;
    std::filesystem::path nativePath_;
    bool useGpuSamples_ = false;
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
