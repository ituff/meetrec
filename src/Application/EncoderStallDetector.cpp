#include "EncoderStallDetector.h"

namespace vsr::app {

void EncoderStallDetector::Reset() {
    declared_ = false;
    armed_ = false;
    lastEncoded_ = 0;
    capturedAtStallStart_ = 0;
    stallStartMs_ = -1;
}

bool EncoderStallDetector::Feed(bool active, std::uint64_t encodedFrames,
                                std::uint64_t capturedFrames, std::int64_t nowMs) {
    if (declared_) return false; // fire once per recording session
    if (!active) {
        // Paused or not consuming: re-arm so the stall clock restarts on the
        // next active sample instead of accumulating across pauses.
        armed_ = false;
        stallStartMs_ = -1;
        return false;
    }
    if (!armed_ || encodedFrames != lastEncoded_) {
        // Writer is advancing (or this is the first observation): re-baseline.
        armed_ = true;
        lastEncoded_ = encodedFrames;
        capturedAtStallStart_ = capturedFrames;
        stallStartMs_ = -1;
        return false;
    }
    // Writer frozen. A stall requires proof that the producer kept feeding:
    // at least one new captured frame since the writer last advanced.
    if (capturedFrames <= capturedAtStallStart_) return false;
    if (stallStartMs_ < 0) stallStartMs_ = nowMs;
    if (nowMs - stallStartMs_ >= stallAfterMs_) {
        declared_ = true;
        return true;
    }
    return false;
}

} // namespace vsr::app
