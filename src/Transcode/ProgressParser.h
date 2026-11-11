// MeetRec - ffmpeg `-progress pipe:1` line parser (ADR-007).
//
// Pure and unit-tested: the runner only feeds complete lines and applies the
// returned sample to the job snapshot.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace vsr::transcode {

struct ProgressSample {
    bool valid = false;
    bool finished = false;
    std::int64_t outTimeUs = 0;
    double speed = 0.0;   // 1.0 = realtime; 0 when ffmpeg reports N/A
    std::uint64_t totalBytes = 0;
    std::int64_t frame = 0;
};

// Recognizes the keys ffmpeg emits for -progress. Unrelated lines return
// valid=false so the caller can ignore them cheaply.
ProgressSample ParseProgressLine(std::string_view line);

} // namespace vsr::transcode
