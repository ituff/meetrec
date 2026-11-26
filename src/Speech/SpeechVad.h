// MeetRec - Silero VAD wrapper (sherpa-onnx C API).
//
// Segments a continuous 16 kHz mono float stream into utterances. The
// detector owns a circular buffer and reports absolute sample offsets since
// the last Reset(), which SpeechSession maps onto recording timestamps.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace vsr::speech {

struct VadSpeechSegment {
    std::int64_t startSample = -1; // absolute since Reset()
    std::vector<float> samples;    // 16 kHz mono
};

class SpeechVad {
public:
    SpeechVad();
    ~SpeechVad();

    SpeechVad(const SpeechVad&) = delete;
    SpeechVad& operator=(const SpeechVad&) = delete;

    bool Create(const std::filesystem::path& model, std::wstring& error);
    void Destroy();

    // Arbitrary chunk length; the implementation buffers internally.
    void Accept(const float* samples, int sampleCount);
    // Pops one completed segment; false when the queue is empty.
    bool Pop(VadSpeechSegment& segment);
    // Forces the trailing speech into a completed segment (stop/pause).
    void Flush();
    void Reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vsr::speech
