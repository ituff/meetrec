// Virtual Screen Recorder - encoder stall detection (NFR-014 defense).
//
// Pure decision logic for the in-process stall watchdog. The sink writer's
// own watermark self-check runs on the worker thread between WriteSample
// calls, so a WriteSample that never returns inside a stalled encoder MFT is
// invisible to it. The watchdog thread samples progress counters instead and
// declares a stall when the writer stops advancing while the capture side
// keeps producing new frames.
//
// Both signals are required: WGC delivers frames only on content change, so a
// static scene legitimately freezes both counters (no stall), while a wedged
// WriteSample freezes only the writer (queue fills, capture keeps feeding).
#pragma once

#include <cstdint>

namespace vsr::app {

class EncoderStallDetector {
public:
    EncoderStallDetector() = default;
    explicit EncoderStallDetector(std::int64_t stallAfterMs) : stallAfterMs_(stallAfterMs) {}

    // Clears the declared latch and the observation window (used between
    // sessions and whenever the recording is not actively consuming).
    void Reset();

    // Feeds one observation. `active` is false while paused or outside the
    // Recording state; inactive samples re-arm the detector instead of
    // accumulating stall time. Returns true exactly once per detected stall.
    bool Feed(bool active, std::uint64_t encodedFrames, std::uint64_t capturedFrames,
              std::int64_t nowMs);

    bool StallDeclared() const noexcept { return declared_; }
    std::int64_t StallAfterMs() const noexcept { return stallAfterMs_; }

private:
    std::int64_t stallAfterMs_ = 15'000;
    bool declared_ = false;
    bool armed_ = false; // has seen the first active sample
    std::uint64_t lastEncoded_ = 0;
    std::uint64_t capturedAtStallStart_ = 0;
    std::int64_t stallStartMs_ = -1;
};

} // namespace vsr::app
