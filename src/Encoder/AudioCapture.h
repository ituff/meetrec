// Virtual Screen Recorder - meeting audio capture (WASAPI loopback + mic).
//
// Captures up to two sources on dedicated threads and mixes them into one
// interleaved 16-bit PCM stream:
//   - loopback: whatever the default render endpoint plays (participants,
//     system/meeting audio);
//   - microphone: the user's own voice from the default capture endpoint
//     (communications role preferred, matching what call apps open).
//
// Each source drains into a timestamped timeline buffer; a mixer thread
// consumes both on a wall-clock grid, zero-fills gaps, and delivers chunks
// with monotonic capture timestamps from the shared IClock so the coordinator
// can align audio with video frames and pause windows.
//
// Best effort by design: callers start it opportunistically and continue
// video-only when no endpoint is available (loopback missing -> microphone
// only, when enabled).
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
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

// Shared mute state between the app and the capture pipeline. Either source
// silences the microphone in the mix; the mic source keeps draining so the
// timestamps stay continuous and no stale audio is replayed after unmute.
struct MicrophoneMuteState {
    std::atomic<bool> manual{false}; // set by the UI/hotkey
    std::atomic<bool> system{false}; // follows the endpoint's mute state

    bool Muted() const noexcept {
        return manual.load(std::memory_order_relaxed) ||
               system.load(std::memory_order_relaxed);
    }
};

struct AudioCaptureOptions {
    bool loopback = true;    // participants / system sound
    bool microphone = false; // the user's own mic
    // Peak level per source (0..1), delivered at the endpoint's packet cadence.
    // The UI uses it for the input meters; it never affects the mix. A muted
    // microphone reports 0 so the meter matches what is recorded.
    using LevelFn = std::function<void(bool microphone, float peak)>;
    LevelFn onLevel;
    // Optional per-source tap for the meeting transcriber: 16-bit mono at the
    // mixed-stream rate (usually 48 kHz), accumulated into ~200 ms chunks.
    // Called on the mixer thread; the callee must copy what it needs and
    // return promptly. A muted microphone delivers silence, matching the mix.
    using SourceAudioFn = std::function<void(bool microphone, const std::int16_t* mono,
                                             std::size_t frames,
                                             std::uint64_t timestampUs)>;
    SourceAudioFn onSourceAudio;
    // Optional shared mute state. A null pointer creates a private state, which
    // keeps the idle level monitor's meter in sync with the endpoint as well.
    std::shared_ptr<MicrophoneMuteState> muteState;
    // Multiplier applied to mic samples before mixing (clamped to int16).
    // VSR_MIC_GAIN overrides this per machine without a settings change.
    float micGain = 1.0f;
    // Endpoint IDs to capture (empty = Windows default for the role). Call
    // apps select their own devices, which often differ from the Windows
    // default; recording the wrong endpoint leaves that source silent.
    std::wstring loopbackDeviceId;
    std::wstring microphoneDeviceId;
};

class AudioCapture {
public:
    // Delivers chunks on the mixer thread; must be cheap or queue internally.
    using SinkFn = std::function<void(AudioChunk&&)>;

    // Default render endpoint mix format (rate/channels), without capturing.
    // A non-empty device ID selects that endpoint (missing ID falls back to
    // the default, keeping a stale setting harmless).
    static Result<AudioFormat> QueryLoopbackFormat(const std::wstring& deviceId = {});
    // Default microphone mix format (rate/channels), without capturing.
    static Result<AudioFormat> QueryMicrophoneFormat(const std::wstring& deviceId = {});

    // Starts the requested sources. `sink` is invoked from the mixer thread
    // until Stop(). Fails only when none of the requested sources is
    // available; a missing loopback with the mic enabled degrades to mic-only.
    static Result<std::unique_ptr<AudioCapture>> Start(const IClock& clock, SinkFn sink,
                                                       const AudioCaptureOptions& options = {});

    ~AudioCapture();

    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    void Stop() noexcept;

private:
    AudioCapture(const IClock& clock, SinkFn sink, AudioCaptureOptions options);

    // Opens one WASAPI stream and drains packets into its timeline buffer
    // until Stop(). Runs on a dedicated thread; kind selects the endpoint.
    void SourceLoop(bool mic);
    // Consumes both timelines on a wall-clock grid, mixes, and emits chunks.
    void MixerLoop();
    // manual || endpoint mute. Safe from the mixer/source threads.
    bool MicrophoneMuted() const noexcept;

    const IClock& clock_;
    SinkFn sink_;
    AudioCaptureOptions options_;
    AudioFormat format_; // target (mixed stream) format

    class TimelineBuffer;
    std::unique_ptr<TimelineBuffer> loopbackBuf_;
    std::unique_ptr<TimelineBuffer> micBuf_;

    std::thread loopbackThread_;
    std::thread micThread_;
    std::thread mixerThread_;
    std::atomic<bool> stopped_{false};
};

} // namespace vsr::encoder
