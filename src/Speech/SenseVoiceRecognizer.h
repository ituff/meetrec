// MeetRec - SenseVoiceSmall (sherpa-onnx int8) offline recognizer wrapper.
//
// One instance owns one recognizer and is not thread-safe: SpeechSession uses
// it from a single worker thread. Recognition is utterance-level (SenseVoice
// is a non-autoregressive CTC model), so callers pass a VAD segment.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace vsr::speech {

struct RecognizedSpeech {
    std::wstring text;
    std::string language; // "<|zh|>" etc. as reported by sherpa-onnx
};

class SenseVoiceRecognizer {
public:
    SenseVoiceRecognizer();
    ~SenseVoiceRecognizer();

    SenseVoiceRecognizer(const SenseVoiceRecognizer&) = delete;
    SenseVoiceRecognizer& operator=(const SenseVoiceRecognizer&) = delete;

    bool Create(const std::filesystem::path& model, const std::filesystem::path& tokens,
                int numThreads, std::wstring& error);
    void Destroy();

    bool IsReady() const noexcept;

    // `samples` are mono float32 at 16 kHz in [-1, 1]. A successful call with
    // empty text is not an error (silence/noise segments).
    bool Recognize(const float* samples, int frameCount, RecognizedSpeech& result,
                   std::wstring& error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vsr::speech
