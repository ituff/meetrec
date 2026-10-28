// Virtual Screen Recorder - video writer abstraction.
//
// Two backends implement the recording sink:
//   - H264Mp4Writer  : Media Foundation sink writer (OS encoders)
//   - FfmpegWriter   : ffmpeg.exe child process (libx264/built-in AAC),
//     selected by Settings.encoderBackend / the --backend CLI flag. The
//     ffmpeg backend bypasses the OS media stack entirely, which contains
//     the encoder-MFT failure family documented in known-limitations.md.
#pragma once

#include <cstdint>
#include <filesystem>

#include <d3d11.h>

#include <Infrastructure/Error.h>
#include <Infrastructure/Result.h>

namespace vsr::encoder {

class IVideoWriter {
public:
    virtual ~IVideoWriter() = default;

    // Submits one NV12 frame (GPU texture) with 100ns-unit timestamp and
    // duration. Implementations stage to CPU as needed.
    virtual Result<void> SubmitFrame(ID3D11Texture2D* nv12Frame, std::int64_t time100Ns,
                                     std::int64_t duration100Ns) = 0;

    // Submits interleaved 16-bit PCM for the audio stream enabled at create.
    virtual Result<void> WriteAudioSample(const std::int16_t* pcm, std::size_t frames,
                                          std::int64_t time100Ns,
                                          std::int64_t duration100Ns) = 0;

    // Watchdog hook: reports EncoderWriteFailed when the sink stopped
    // consuming (NFR-007/NFR-014 defense). Cheap; slow cadence.
    virtual Result<void> QueueHealth() = 0;

    // Drains pending data, finalizes the container, closes the file.
    // Idempotent (FR-073). Bounded: a backend that cannot finalize fails
    // within its internal deadline instead of blocking forever.
    virtual Result<void> Finalize() = 0;

    virtual bool HasAudio() const noexcept = 0;
    virtual bool IsFinalized() const noexcept { return false; }
    virtual std::uint64_t WrittenFrames() const noexcept = 0;
};

} // namespace vsr::encoder
