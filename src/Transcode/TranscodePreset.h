// MeetRec - offline transcoding presets (ADR-007).
//
// The compressor targets the same screen-content recipe as live recording
// (capped CRF, still-image tuning, long GOP) but never touches the rawvideo
// pipe path in Encoder/FfmpegWriter. Values mirror encoder::PresetParams; the
// unit tests assert the two tables stay in sync.
#pragma once

#include <cstdint>

namespace vsr::transcode {

enum class Quality { Low, Standard, High };

// Output frame size. Keep uses the source dimensions (never upscales);
// P720/P1080 fit the source into a 1280x720 / 1920x1080 box.
enum class Resolution { Keep, P720, P1080 };

struct EncodeOptions {
    int crf = 26;
    std::uint32_t maxrateBps = 2'000'000;
    std::uint32_t gopSeconds = 5;
    bool screenContentTune = true;
    std::uint32_t audioBitrateBps = 64'000;
    std::uint32_t audioChannels = 1;
};

EncodeOptions OptionsForQuality(Quality quality) noexcept;

// Stable names used by settings and logs; unknown values mean Standard.
const char* ToString(Quality quality) noexcept;
Quality QualityFromString(const char* name) noexcept;
const char* ToString(Resolution resolution) noexcept;
Resolution ResolutionFromString(const char* name) noexcept;

// Target dimensions for a source frame, rounded to even pixels. Returns 0x0
// when the source dimensions are unknown and Resolution::Keep is requested.
void TargetDimensions(Resolution resolution, std::uint32_t sourceWidth,
                      std::uint32_t sourceHeight, std::uint32_t& widthOut,
                      std::uint32_t& heightOut) noexcept;

// Keyframe interval in frames for a source frame rate (150 frames when the
// frame rate is unknown).
std::uint32_t GopFrames(const EncodeOptions& options, double frameRate) noexcept;

// Upper bound on the output size for a duration, from the rate ceilings
// (video maxrate + audio bitrate, 5% container overhead). The real file is
// usually smaller because capped CRF only spends the ceiling on busy frames.
std::uint64_t EstimatedOutputBytes(const EncodeOptions& options,
                                   std::int64_t durationMs) noexcept;

} // namespace vsr::transcode
