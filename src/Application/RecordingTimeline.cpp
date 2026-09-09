#include "RecordingTimeline.h"

namespace vsr::app {

void RecordingTimeline::StartAt(std::int64_t monotonic100Ns) noexcept {
    start_ = monotonic100Ns;
    pauseBegan_ = -1;
    accumulatedPause_ = 0;
    pauseWatermark_ = monotonic100Ns;
    paused_ = false;
}

void RecordingTimeline::PauseAt(std::int64_t monotonic100Ns) noexcept {
    if (paused_ || pauseBegan_ >= 0) return;
    pauseBegan_ = monotonic100Ns;
    paused_ = true;
    pauseWatermark_ = monotonic100Ns;
}

void RecordingTimeline::ResumeAt(std::int64_t monotonic100Ns) noexcept {
    if (!paused_) return;
    if (pauseBegan_ >= 0 && monotonic100Ns > pauseBegan_) {
        accumulatedPause_ += monotonic100Ns - pauseBegan_;
    }
    pauseBegan_ = -1;
    paused_ = false;
    pauseWatermark_ = monotonic100Ns;
}

std::int64_t RecordingTimeline::ActiveElapsed100Ns(std::int64_t monotonic100Ns) const noexcept {
    std::int64_t elapsed = monotonic100Ns - start_ - accumulatedPause_;
    if (paused_ && pauseBegan_ >= 0) {
        // While paused, active time freezes at the pause point.
        elapsed -= monotonic100Ns - pauseBegan_;
    }
    if (elapsed < 0) elapsed = 0;
    return elapsed;
}

std::int64_t RecordingTimeline::OutputTimestamp100Ns(std::int64_t monotonic100Ns) const noexcept {
    return ActiveElapsed100Ns(monotonic100Ns);
}

} // namespace vsr::app
