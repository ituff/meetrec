// Recording timeline tests (T041): timestamps exclude paused duration and are
// monotonic (FR-055, AC-005); all values in 100ns units.
#include "TestFramework.h"
#include <Application/RecordingTimeline.h>

using vsr::app::RecordingTimeline;

VSR_TEST(Timeline, ElapsedExcludesPaused) {
    RecordingTimeline t;
    t.StartAt(0);
    VSR_CHECK_EQ(t.ActiveElapsed100Ns(10'000'000), 10'000'000); // 1s active

    t.PauseAt(10'000'000);           // pause at 1s
    VSR_CHECK_EQ(t.ActiveElapsed100Ns(20'000'000), 10'000'000); // frozen at 1s

    t.ResumeAt(20'000'000);          // paused for 1s
    VSR_CHECK_EQ(t.ActiveElapsed100Ns(30'000'000), 20'000'000); // 2s active total
}

VSR_TEST(Timeline, MultiplePauseIntervalsAccumulate) {
    RecordingTimeline t;
    t.StartAt(0);
    t.PauseAt(1'000'000);
    t.ResumeAt(3'000'000);   // +2s paused
    t.PauseAt(5'000'000);
    t.ResumeAt(9'000'000);   // +4s paused
    // 9s wall - 6s paused = 3s active
    VSR_CHECK_EQ(t.ActiveElapsed100Ns(9'000'000), 3'000'000);
}

VSR_TEST(Timeline, OutputTimestampMatchesActiveTime) {
    RecordingTimeline t;
    t.StartAt(1'000'000);
    t.PauseAt(2'000'000);
    t.ResumeAt(4'000'000);
    VSR_CHECK_EQ(t.OutputTimestamp100Ns(5'000'000), 2'000'000); // 5-1-2
}

VSR_TEST(Timeline, StaleFrameWatermarkAdvancesAtBoundaries) {
    RecordingTimeline t;
    t.StartAt(0);
    VSR_CHECK_EQ(t.StaleFrameWatermark100Ns(), 0);
    t.PauseAt(500);
    VSR_CHECK_EQ(t.StaleFrameWatermark100Ns(), 500);
    t.ResumeAt(900);
    VSR_CHECK_EQ(t.StaleFrameWatermark100Ns(), 900);
}

VSR_TEST(Timeline, NeverNegative) {
    RecordingTimeline t;
    t.StartAt(100);
    VSR_CHECK_EQ(t.ActiveElapsed100Ns(50), 0);
}

VSR_TEST(Timeline, PauseWhilePausedIsNoOp) {
    RecordingTimeline t;
    t.StartAt(0);
    t.PauseAt(100);
    t.PauseAt(500); // ignored
    t.ResumeAt(200);
    VSR_CHECK_EQ(t.AccumulatedPause100Ns(), 100);
}

VSR_TEST(Timeline, ResumeBeforePauseClampsToZero) {
    RecordingTimeline t;
    t.StartAt(0);
    t.PauseAt(100);
    t.ResumeAt(50); // resume "before" the pause point
    VSR_CHECK_EQ(t.AccumulatedPause100Ns(), 0);
}
