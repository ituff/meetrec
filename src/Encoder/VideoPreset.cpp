#include "VideoPreset.h"

#include <unordered_map>

namespace vsr::encoder {

const char* ToString(QualityPreset preset) noexcept {
    switch (preset) {
        case QualityPreset::Low: return "low";
        case QualityPreset::Standard: return "standard";
        case QualityPreset::High: return "high";
    }
    return "standard";
}

QualityPreset FromString(const char* name) noexcept {
    if (name != nullptr) {
        if (std::string_view(name) == "low") return QualityPreset::Low;
        if (std::string_view(name) == "high") return QualityPreset::High;
    }
    return QualityPreset::Standard;
}

PresetParams GetPresetParams(QualityPreset preset) noexcept {
    switch (preset) {
        case QualityPreset::Low:
            return {1280, 720, 30, 3'000'000};
        case QualityPreset::High:
            return {1920, 1080, 60, 12'000'000};
        case QualityPreset::Standard:
        default:
            return {1920, 1080, 30, 6'000'000};
    }
}

std::int64_t FrameDuration100Ns(std::uint32_t fps) noexcept {
    if (fps == 0) fps = 30;
    return 10'000'000 / fps;
}

Result<void> ValidatePresetTable() {
    struct Row {
        const char* name;
        PresetParams params;
    };
    const Row rows[] = {
        {"low", GetPresetParams(QualityPreset::Low)},
        {"standard", GetPresetParams(QualityPreset::Standard)},
        {"high", GetPresetParams(QualityPreset::High)},
    };
    std::unordered_map<std::string_view, int> seen;
    for (const auto& row : rows) {
        if (row.params.width == 0 || row.params.height == 0 || row.params.fps < 15 ||
            row.params.fps > 120 || row.params.bitrateBps < 500'000) {
            return Error(ErrorCode::EncoderUnavailable,
                         L"Internal preset configuration error.",
                         L"Preset table row is out of the valid range.");
        }
        if (!seen.emplace(row.name, 1).second) {
            return Error(ErrorCode::EncoderUnavailable,
                         L"Internal preset configuration error.",
                         L"Duplicate preset name in table.");
        }
    }
    // Spec values (spec.md §7.3): the three rows must match exactly.
    if (rows[0].params.width != 1280 || rows[0].params.fps != 30 ||
        rows[0].params.bitrateBps != 3'000'000 || rows[1].params.width != 1920 ||
        rows[1].params.fps != 30 || rows[1].params.bitrateBps != 6'000'000 ||
        rows[2].params.width != 1920 || rows[2].params.fps != 60 ||
        rows[2].params.bitrateBps != 12'000'000) {
        return Error(ErrorCode::EncoderUnavailable,
                     L"Internal preset configuration error.",
                     L"Preset table deviates from spec.md section 7.3.");
    }
    return Result<void>{};
}

} // namespace vsr::encoder
