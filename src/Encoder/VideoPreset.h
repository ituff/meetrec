// Virtual Screen Recorder - quality presets (spec.md §7.3, FR-051).
#pragma once

#include <cstdint>

#include <Infrastructure/Error.h>
#include <Infrastructure/Result.h>

namespace vsr::encoder {

enum class QualityPreset { Low, Standard, High };

struct PresetParams {
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    std::uint32_t fps = 30;
    std::uint32_t bitrateBps = 6'000'000;
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
