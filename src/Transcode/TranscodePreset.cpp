#include "TranscodePreset.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace vsr::transcode {

namespace {

constexpr std::uint32_t kP720Width = 1280;
constexpr std::uint32_t kP720Height = 720;
constexpr std::uint32_t kP1080Width = 1920;
constexpr std::uint32_t kP1080Height = 1080;

std::uint32_t EvenFloor(std::uint32_t value) noexcept {
    return value & ~1u;
}

void FitInto(std::uint32_t boxWidth, std::uint32_t boxHeight, std::uint32_t sourceWidth,
             std::uint32_t sourceHeight, std::uint32_t& widthOut,
             std::uint32_t& heightOut) noexcept {
    if (sourceWidth == 0 || sourceHeight == 0) {
        widthOut = 0;
        heightOut = 0;
        return;
    }
    const double scale = std::min(1.0, std::min(static_cast<double>(boxWidth) / sourceWidth,
                                                static_cast<double>(boxHeight) / sourceHeight));
    widthOut = EvenFloor(static_cast<std::uint32_t>(std::lround(sourceWidth * scale)));
    heightOut = EvenFloor(static_cast<std::uint32_t>(std::lround(sourceHeight * scale)));
    widthOut = std::max(2u, widthOut);
    heightOut = std::max(2u, heightOut);
}

} // namespace

EncodeOptions OptionsForQuality(Quality quality) noexcept {
    switch (quality) {
        case Quality::Low:
            // Mirrors encoder::QualityPreset::Low.
            return {.crf = 28,
                    .maxrateBps = 1'000'000,
                    .gopSeconds = 5,
                    .screenContentTune = true,
                    .audioBitrateBps = 48'000,
                    .audioChannels = 1};
        case Quality::High:
            // Mirrors encoder::QualityPreset::High (motion-oriented tier).
            return {.crf = 22,
                    .maxrateBps = 6'000'000,
                    .gopSeconds = 5,
                    .screenContentTune = false,
                    .audioBitrateBps = 128'000,
                    .audioChannels = 2};
        case Quality::Standard:
        default:
            // Mirrors encoder::QualityPreset::Standard (meeting default).
            return {.crf = 26,
                    .maxrateBps = 2'000'000,
                    .gopSeconds = 5,
                    .screenContentTune = true,
                    .audioBitrateBps = 64'000,
                    .audioChannels = 1};
    }
}

const char* ToString(Quality quality) noexcept {
    switch (quality) {
        case Quality::Low: return "low";
        case Quality::High: return "high";
        case Quality::Standard: return "standard";
    }
    return "standard";
}

Quality QualityFromString(const char* name) noexcept {
    if (name != nullptr) {
        if (std::string_view(name) == "low") return Quality::Low;
        if (std::string_view(name) == "high") return Quality::High;
    }
    return Quality::Standard;
}

const char* ToString(Resolution resolution) noexcept {
    switch (resolution) {
        case Resolution::P720: return "720p";
        case Resolution::P1080: return "1080p";
        case Resolution::Keep: return "keep";
    }
    return "keep";
}

Resolution ResolutionFromString(const char* name) noexcept {
    if (name != nullptr) {
        if (std::string_view(name) == "720p") return Resolution::P720;
        if (std::string_view(name) == "1080p") return Resolution::P1080;
    }
    return Resolution::Keep;
}

void TargetDimensions(Resolution resolution, std::uint32_t sourceWidth,
                      std::uint32_t sourceHeight, std::uint32_t& widthOut,
                      std::uint32_t& heightOut) noexcept {
    switch (resolution) {
        case Resolution::P720:
            FitInto(kP720Width, kP720Height, sourceWidth, sourceHeight, widthOut, heightOut);
            return;
        case Resolution::P1080:
            FitInto(kP1080Width, kP1080Height, sourceWidth, sourceHeight, widthOut, heightOut);
            return;
        case Resolution::Keep:
        default:
            widthOut = EvenFloor(sourceWidth);
            heightOut = EvenFloor(sourceHeight);
            return;
    }
}

std::uint32_t GopFrames(const EncodeOptions& options, double frameRate) noexcept {
    if (frameRate <= 1.0 || frameRate > 240.0) {
        return std::max(1u, options.gopSeconds * 30u);
    }
    return std::max(1u, static_cast<std::uint32_t>(
                            std::lround(frameRate * options.gopSeconds)));
}

std::uint64_t EstimatedOutputBytes(const EncodeOptions& options,
                                   std::int64_t durationMs) noexcept {
    if (durationMs <= 0) return 0;
    const double seconds = static_cast<double>(durationMs) / 1000.0;
    const double videoBits = seconds * static_cast<double>(options.maxrateBps);
    const double audioBits = seconds * static_cast<double>(options.audioBitrateBps);
    return static_cast<std::uint64_t>((videoBits + audioBits) / 8.0 * 1.05);
}

} // namespace vsr::transcode
