// Encoder stall watchdog tests: the pure detector must require BOTH signals
// (writer frozen + capture still producing), re-arm across pauses, and fire
// exactly once at/after the threshold.
#include "TestFramework.h"
#include <Application/EncoderStallDetector.h>

using vsr::app::EncoderStallDetector;

namespace {

// Convenience: feed one sample, advancing time 1s per call.
struct Feed {
    EncoderStallDetector& d;
    bool operator()(bool active, std::uint64_t encoded, std::uint64_t captured,
                    std::int64_t tMs) const {
        return d.Feed(active, encoded, captured, tMs);
    }
};

} // namespace

VSR_TEST(Watchdog, NoFireWhileWriterAdvances) {
    EncoderStallDetector d(15'000);
    for (int t = 0; t <= 60; ++t) {
        // Both counters advance every second: healthy recording.
        VSR_CHECK(!d.Feed(true, static_cast<std::uint64_t>(t * 30),
                          static_cast<std::uint64_t>(t * 30), t * 1'000));
    }
    VSR_CHECK(!d.StallDeclared());
}

VSR_TEST(Watchdog, FiresWhenWriterFrozenWhileCaptureProduces) {
    EncoderStallDetector d(15'000);
    // Healthy for 5s, then the writer wedges at 150 encoded while capture
    // keeps producing at 30 fps.
    for (int t = 0; t <= 5; ++t) {
        VSR_CHECK(!d.Feed(true, static_cast<std::uint64_t>(t * 30),
                          static_cast<std::uint64_t>(t * 30), t * 1'000));
    }
    bool fired = false;
    for (int t = 6; t <= 30; ++t) {
        if (d.Feed(true, 150, static_cast<std::uint64_t>(t * 30), t * 1'000)) {
            fired = true;
            // Threshold is 15s of frozen writer; the fire may only happen at
            // the sample at/after 5s + 15s = 20s.
            VSR_CHECK(t * 1'000 >= 20'000);
            break;
        }
    }
    VSR_CHECK(fired);
    VSR_CHECK(d.StallDeclared());
}

VSR_TEST(Watchdog, NoFireWhenBothCountersFrozen) {
    EncoderStallDetector d(15'000);
    for (int t = 0; t <= 5; ++t) {
        d.Feed(true, static_cast<std::uint64_t>(t * 30), static_cast<std::uint64_t>(t * 30),
               t * 1'000);
    }
    // Static scene: WGC delivers nothing new, writer has nothing to encode.
    for (int t = 6; t <= 120; ++t) {
        VSR_CHECK(!d.Feed(true, 150, 150, t * 1'000));
    }
    VSR_CHECK(!d.StallDeclared());
}

VSR_TEST(Watchdog, PauseReArmsTheStallClock) {
    EncoderStallDetector d(15'000);
    for (int t = 0; t <= 5; ++t) {
        d.Feed(true, static_cast<std::uint64_t>(t * 30), static_cast<std::uint64_t>(t * 30),
               t * 1'000);
    }
    // 10s of frozen writer with production: stall clock runs to 10s.
    for (int t = 6; t <= 15; ++t) {
        VSR_CHECK(!d.Feed(true, 150, static_cast<std::uint64_t>(t * 30), t * 1'000));
    }
    // Pause for a minute (active=false re-arms); resume with the writer still
    // frozen: the clock must restart, not accumulate.
    for (int t = 16; t <= 76; ++t) {
        VSR_CHECK(!d.Feed(false, 150, static_cast<std::uint64_t>(t * 30), t * 1'000));
    }
    for (int t = 77; t <= 88; ++t) {
        // Only 11s since resume: below the 15s threshold.
        VSR_CHECK(!d.Feed(true, 150, static_cast<std::uint64_t>(t * 30), t * 1'000));
    }
    VSR_CHECK(!d.Feed(true, 150, 90 * 30, 89 * 1'000)); // 11s since re-arm: below
    VSR_CHECK(!d.StallDeclared());
    VSR_CHECK(!d.Feed(true, 150, 91 * 30, 92 * 1'000)); // 14s: still below
    VSR_CHECK(d.Feed(true, 150, 93 * 30, 93 * 1'000)); // 15s since re-arm: fire
    VSR_CHECK(d.StallDeclared());
}

VSR_TEST(Watchdog, FiresExactlyOnce) {
    EncoderStallDetector d(15'000);
    d.Feed(true, 0, 0, 0);
    bool fired = false;
    for (int t = 1; t <= 60; ++t) {
        if (d.Feed(true, 0, static_cast<std::uint64_t>(t * 30), t * 1'000)) {
            VSR_CHECK(!fired); // at most one fire
            fired = true;
        }
    }
    VSR_CHECK(fired);
    // Post-fire feeds never re-fire.
    for (int t = 61; t <= 70; ++t) {
        VSR_CHECK(!d.Feed(true, 0, static_cast<std::uint64_t>(t * 30), t * 1'000));
    }
}

VSR_TEST(Watchdog, SingleNewFrameStartsTheClock) {
    EncoderStallDetector d(15'000);
    d.Feed(true, 100, 100, 0);
    // Exactly one new captured frame while the writer is frozen: the stall
    // clock starts (capture kept feeding past the wedge).
    VSR_CHECK(!d.Feed(true, 100, 101, 1'000));
    for (int t = 2; t <= 14; ++t) {
        VSR_CHECK(!d.Feed(true, 100, 101, t * 1'000));
    }
    VSR_CHECK(d.Feed(true, 100, 200, 16'000));
}
