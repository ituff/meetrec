#include "SpeechResampler.h"

#include <algorithm>
#include <cmath>

namespace vsr::speech {

namespace {

constexpr std::size_t kTaps = 63;
constexpr double kPi = 3.14159265358979323846;

double Sinc(double x) {
    if (std::fabs(x) < 1e-9) return 1.0;
    return std::sin(kPi * x) / (kPi * x);
}

} // namespace

SpeechResampler::SpeechResampler(std::uint32_t inputRate, std::uint32_t outputRate)
    : inputRate_(inputRate),
      outputRate_(outputRate),
      passthrough_(inputRate == outputRate) {
    step_ = static_cast<double>(inputRate_) / static_cast<double>(outputRate_);
    if (!passthrough_) DesignFilter();
}

void SpeechResampler::DesignFilter() {
    // Windowed-sinc low-pass at 45% of the lower rate, evaluated at the input
    // rate. 63 taps keep the passband flat enough for 16 kHz speech while the
    // group delay (31 samples, <1 ms at 48 kHz) is irrelevant here.
    const double minRate = static_cast<double>(std::min(inputRate_, outputRate_));
    const double cutoff = 0.45 * minRate / static_cast<double>(inputRate_);
    const double center = static_cast<double>(kTaps - 1) / 2.0;

    taps_.assign(kTaps, 0.0f);
    double sum = 0.0;
    for (std::size_t i = 0; i < kTaps; ++i) {
        const double n = static_cast<double>(i);
        const double window = 0.54 - 0.46 * std::cos(2.0 * kPi * n /
                                                      static_cast<double>(kTaps - 1));
        const double value = 2.0 * cutoff * Sinc(2.0 * cutoff * (n - center)) * window;
        taps_[i] = static_cast<float>(value);
        sum += value;
    }
    if (std::fabs(sum) < 1e-9) {
        taps_.assign(kTaps, 0.0f);
        taps_[kTaps / 2] = 1.0f;
    } else {
        for (float& tap : taps_) tap = static_cast<float>(tap / sum);
    }
    delay_.assign(kTaps, 0.0f);
    history_.clear();
}

void SpeechResampler::Reset() {
    position_ = 0.0;
    history_.clear();
    if (!passthrough_) {
        std::fill(delay_.begin(), delay_.end(), 0.0f);
    }
}

void SpeechResampler::Process(const std::int16_t* mono, std::size_t frames,
                              std::vector<float>& out) {
    if (mono == nullptr || frames == 0) return;

    if (passthrough_) {
        out.reserve(out.size() + frames);
        for (std::size_t i = 0; i < frames; ++i) {
            out.push_back(static_cast<float>(mono[i]) / 32768.0f);
        }
        return;
    }

    history_.reserve(history_.size() + frames);
    for (std::size_t i = 0; i < frames; ++i) {
        // Shift the delay line one sample and push the new input. kTaps is
        // small, so the linear shift is cheaper than a circular index.
        std::move(delay_.begin() + 1, delay_.end(), delay_.begin());
        delay_.back() = static_cast<float>(mono[i]) / 32768.0f;

        float filtered = 0.0f;
        for (std::size_t k = 0; k < kTaps; ++k) {
            filtered += taps_[k] * delay_[k];
        }
        history_.push_back(filtered);
    }

    const double historySize = static_cast<double>(history_.size());
    while (position_ + 1.0 < historySize) {
        const std::size_t index = static_cast<std::size_t>(position_);
        const float fraction = static_cast<float>(position_ - static_cast<double>(index));
        const float value =
            history_[index] + (history_[index + 1] - history_[index]) * fraction;
        out.push_back(value);
        position_ += step_;
    }

    // Keep only the sample before the next fractional position (and the one
    // after it); the earlier filtered samples can never be read again.
    const std::size_t drop = std::min(history_.size(),
                                      static_cast<std::size_t>(position_));
    if (drop > 0) {
        history_.erase(history_.begin(),
                       history_.begin() + static_cast<std::ptrdiff_t>(drop));
        position_ -= static_cast<double>(drop);
    }
}

} // namespace vsr::speech
