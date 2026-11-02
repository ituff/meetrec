#include "VideoPreset.h"

#include <cstddef>
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
            return {.width = 1280,
                    .height = 720,
                    .fps = 30,
                    .crf = 28,
                    .bitrateBps = 1'000'000,
                    .gopSeconds = 5,
                    .screenContentTune = true,
                    .audioBitrateBps = 48'000,
                    .audioChannels = 1};
        case QualityPreset::High:
            return {.width = 1920,
                    .height = 1080,
                    .fps = 60,
                    .crf = 22,
                    .bitrateBps = 6'000'000,
                    .gopSeconds = 5,
                    .screenContentTune = false,
                    .audioBitrateBps = 128'000,
                    .audioChannels = 2};
        case QualityPreset::Standard:
        default:
            return {.width = 1920,
                    .height = 1080,
                    .fps = 30,
                    .crf = 26,
                    .bitrateBps = 2'000'000,
                    .gopSeconds = 5,
                    .screenContentTune = true,
                    .audioBitrateBps = 64'000,
                    .audioChannels = 1};
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
            row.params.fps > 120 || row.params.bitrateBps < 500'000 ||
            row.params.crf < 0 || row.params.crf > 51 || row.params.gopSeconds < 1 ||
            row.params.gopSeconds > 60 || row.params.audioBitrateBps < 32'000 ||
            row.params.audioBitrateBps > 320'000 ||
            (row.params.audioChannels != 1 && row.params.audioChannels != 2)) {
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
    struct SpecRow {
        std::uint32_t width;
        std::uint32_t fps;
        int crf;
        std::uint32_t bitrateBps;
        std::uint32_t gopSeconds;
        std::uint32_t audioBitrateBps;
        std::uint32_t audioChannels;
    };
    const SpecRow spec[] = {
        {1280, 30, 28, 1'000'000, 5, 48'000, 1},
        {1920, 30, 26, 2'000'000, 5, 64'000, 1},
        {1920, 60, 22, 6'000'000, 5, 128'000, 2},
    };
    for (std::size_t i = 0; i < 3; ++i) {
        const PresetParams& p = rows[i].params;
        if (p.width != spec[i].width || p.fps != spec[i].fps || p.crf != spec[i].crf ||
            p.bitrateBps != spec[i].bitrateBps || p.gopSeconds != spec[i].gopSeconds ||
            p.audioBitrateBps != spec[i].audioBitrateBps ||
            p.audioChannels != spec[i].audioChannels) {
            return Error(ErrorCode::EncoderUnavailable,
                         L"Internal preset configuration error.",
                         L"Preset table deviates from spec.md section 7.3.");
        }
    }
    return Result<void>{};
}

} // namespace vsr::encoder
