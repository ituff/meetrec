// Virtual Screen Recorder - quality presets (spec.md §7.3, FR-051).
#pragma once

#include <cstdint>

#include <Infrastructure/Error.h>
#include <Infrastructure/Result.h>

namespace vsr::encoder {

enum class QualityPreset { Low, Standard, High };

// Meeting-oriented encode parameters. The size budget is benchmarked against a
// Teams cloud recording (H.264 Constrained Baseline, 1920x1080, 16 FPS,
// 326 kbps video, 64 kbps mono AAC): 25 minutes of meeting footage cost 74 MB
// there. Two properties of screen content drive that number, and both are
// preset data here instead of being hard-coded in an encoder backend:
//
//   - Constant quality with a ceiling, not a bitrate target. Slides and
//     static windows need very few bits; a pure ABR encoder keeps spending its
//     full target on an unchanged frame.
//   - A long keyframe interval. A 1080p keyframe re-codes the whole frame at
//     full quality, and meetings are mostly static: on measured meeting
//     content the same CRF cost 472 kbps at a 2 s GOP, 208 kbps at 5 s and
//     119 kbps at 10 s.
//
// Frame rate is deliberately not a size lever: at constant quality, encoding
// the same screen at 15 FPS cost more than at 30 FPS (960 vs 829 kbps) because
// each frame then has to carry twice the on-screen change.
struct PresetParams {
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    std::uint32_t fps = 30;
    // x264 constant-quality target.
    int crf = 26;
    // x264 rate ceiling (maxrate), and the ABR target for hardware and Media
    // Foundation encoders, which have no constant-quality mode.
    std::uint32_t bitrateBps = 2'000'000;
    // Keyframe interval in seconds.
    std::uint32_t gopSeconds = 5;
    // x264 "stillimage" tuning (weaker deblocking, more references, biased
    // adaptive quantisation): keeps text edges intact on shared slides.
    // Off for the motion-oriented tier, where blocking would be more visible.
    bool screenContentTune = true;
    // Meeting speech fits a mono 64 kbps AAC track (Teams records 65 kbps
    // mono); the high-motion tier keeps stereo for music and media playback.
    std::uint32_t audioBitrateBps = 64'000;
    std::uint32_t audioChannels = 1;
};

const char* ToString(QualityPreset preset) noexcept;
// Accepts "low" / "standard" / "high"; anything else falls back to Standard.
QualityPreset FromString(const char* name) noexcept;
PresetParams GetPresetParams(QualityPreset preset) noexcept;

// Nominal frame duration in 100-nanosecond Media Foundation units.
std::int64_t FrameDuration100Ns(std::uint32_t fps) noexcept;

// Validates the full preset table invariants at startup (unit-tested).
Result<void> ValidatePresetTable();

} // namespace vsr::encoder
