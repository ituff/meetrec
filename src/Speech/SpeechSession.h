// MeetRec - meeting transcription session (P1).
//
// Owns the speech worker: per-source ring queue -> 16 kHz resampler -> Silero
// VAD -> SenseVoiceSmall int8 -> finalized transcript cues. The recorder feeds
// it from the AudioCapture mixer thread; model loading and recognition happen
// on a dedicated below-normal-priority worker so recording never waits for
// speech.
//
// Source separation is deliberate: the WASAPI loopback (participants) and the
// microphone (local speaker) are spoken to the session separately, which gives
// a free "Them"/"Me" label and avoids recognizing a mixed stream.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include <Infrastructure/Result.h>

#include "SpeechModelPack.h"
#include "TranscriptStore.h"

namespace vsr::speech {

struct SpeechSessionOptions {
    SpeechModelPaths model;
    std::filesystem::path basePath; // recording path without extension
    std::uint32_t sourceSampleRate = 48000;
    bool microphone = false; // capture and recognize the mic source too
    bool chinese = true;     // transcript labels/content language
    int numThreads = 4;      // SenseVoice intra-op threads
};

class SpeechSession {
public:
    enum class State { Loading, Ready, Failed, Stopped };

    struct Status {
        State state = State::Loading;
        std::size_t segments = 0;
        std::wstring detail;
    };

    static Result<std::unique_ptr<SpeechSession>> Start(
        const SpeechSessionOptions& options);

    ~SpeechSession();

    SpeechSession(const SpeechSession&) = delete;
    SpeechSession& operator=(const SpeechSession&) = delete;

    // Called from the AudioCapture mixer thread. `mono` is 16-bit mono at
    // options.sourceSampleRate; `outputTimestampUs` is the recording timeline
    // position of its first sample (pause already excluded). The samples are
    // copied into the bounded queue; this never blocks on recognition.
    void PushAudio(bool microphone, const std::int16_t* mono, std::size_t frames,
                   std::int64_t outputTimestampUs);

    // Ordered control messages: pause flushes the in-flight utterance before
    // the boundary; resume starts a new VAD epoch.
    void SetPaused(bool paused);

    // Test/CLI seam: waits for the model load to finish (or fail).
    bool WaitForReady(int timeoutMs) const;

    // Drains queued audio, flushes the VAD and stops the worker. Bounded by
    // the longest queued utterance; recognition itself is not interruptible.
    void Stop();

    // Writes <base>.srt / <base>.txt after Stop(). Safe to call once.
    TranscriptStore::Written Finalize();

    Status GetStatus() const;

private:
    struct Impl;
    explicit SpeechSession(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace vsr::speech
