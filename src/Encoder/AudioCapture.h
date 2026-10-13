// Virtual Screen Recorder - system audio capture (WASAPI loopback).
//
// Captures whatever the default render endpoint plays (system/meeting audio)
// as 16-bit interleaved PCM chunks on a dedicated thread, with monotonic
// capture timestamps from the shared IClock so the coordinator can align
// audio with video frames and pause windows.
//
// Best effort by design: callers start it opportunistically and continue
// video-only when no render endpoint or loopback is available.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include <Infrastructure/Clock.h>
#include <Infrastructure/Error.h>
#include <Infrastructure/Result.h>

namespace vsr::encoder {

struct AudioFormat {
    std::uint32_t sampleRate = 48000;
    std::uint32_t channels = 2;
};

// Interleaved 16-bit PCM chunk with a monotonic capture timestamp.
struct AudioChunk {
    std::vector<std::int16_t> pcm;
    std::uint64_t frames = 0;      // per-channel frames in this chunk
    std::uint64_t timestampUs = 0; // monotonic capture timestamp
};

class AudioCapture {
public:
    // Delivers chunks on the capture thread; must be cheap or queue internally.
    using SinkFn = std::function<void(AudioChunk&&)>;

    // Default render endpoint mix format (rate/channels), without capturing.
    static Result<AudioFormat> QueryLoopbackFormat();

    // Starts loopback capture of the default render endpoint. `sink` is
    // invoked from the capture thread until Stop().
    static Result<std::unique_ptr<AudioCapture>> Start(const IClock& clock, SinkFn sink);

    ~AudioCapture();

    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    void Stop() noexcept;

private:
    AudioCapture(const IClock& clock, SinkFn sink);
    void CaptureLoop();

    const IClock& clock_;
    SinkFn sink_;
    std::thread thread_;
    std::atomic<bool> stopped_{false};
};

} // namespace vsr::encoder