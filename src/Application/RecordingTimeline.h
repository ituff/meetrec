// Virtual Screen Recorder - recording timeline (FR-055, FR-071, FR-072;
// design.md §4.8 time model).
//
// output_timestamp = monotonic_now - recording_start - accumulated_paused
//
// All values are in 100-nanosecond units (Media Foundation convention) and
// derived from a monotonic clock. Pause keeps capture alive but excludes the
// paused interval from output timestamps; stale frames are discarded at the
// pause boundary by the coordinator.
#pragma once

#include <cstdint>

namespace vsr::app {

class RecordingTimeline {
public:
    void StartAt(std::int64_t monotonic100Ns) noexcept;
    void PauseAt(std::int64_t monotonic100Ns) noexcept;
    void ResumeAt(std::int64_t monotonic100Ns) noexcept;

    // Elapsed ACTIVE recording time (paused intervals excluded), in 100ns.
    std::int64_t ActiveElapsed100Ns(std::int64_t monotonic100Ns) const noexcept;

    // Output timestamp for a frame captured at `monotonic100Ns`.
    std::int64_t OutputTimestamp100Ns(std::int64_t monotonic100Ns) const noexcept;

    bool IsPaused() const noexcept { return paused_; }
    std::int64_t AccumulatedPause100Ns() const noexcept { return accumulatedPause_; }

    // Clears frames captured before this monotonic point (pause boundary).
    std::int64_t StaleFrameWatermark100Ns() const noexcept { return pauseWatermark_; }

private:
    std::int64_t start_ = 0;
    std::int64_t pauseBegan_ = -1;
    std::int64_t accumulatedPause_ = 0;
    std::int64_t pauseWatermark_ = 0;
    bool paused_ = false;
};

} // namespace vsr::app
