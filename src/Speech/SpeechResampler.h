// MeetRec - speech frontend resampler (P1 transcription).
//
// Converts mono 16-bit PCM at the capture endpoint rate (usually 48 kHz) to
// the 16 kHz mono float stream SenseVoice expects. Stateful: the low-pass
// filter history and the fractional output position carry across calls, so
// chunks must be fed in order on one thread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vsr::speech {

class SpeechResampler {
public:
    explicit SpeechResampler(std::uint32_t inputRate, std::uint32_t outputRate = 16000);

    // Drops all filter/interpolation state. The next Process() starts a new
    // stream epoch (pause/resume, dropped audio, new source).
    void Reset();

    // Appends 16 kHz samples to `out`; existing contents are preserved so
    // callers can accumulate a whole VAD segment.
    void Process(const std::int16_t* mono, std::size_t frames, std::vector<float>& out);

    std::uint32_t InputRate() const noexcept { return inputRate_; }
    std::uint32_t OutputRate() const noexcept { return outputRate_; }

private:
    void DesignFilter();

    std::uint32_t inputRate_;
    std::uint32_t outputRate_;
    bool passthrough_ = false;
    std::vector<float> taps_;
    std::vector<float> delay_;
    std::vector<float> history_;
    double position_ = 0.0; // fractional position in history_
    double step_ = 1.0;     // input samples per output sample
};

} // namespace vsr::speech
