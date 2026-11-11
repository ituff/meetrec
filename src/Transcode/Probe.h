// MeetRec - ffprobe integration for the compressor (ADR-007).
//
// Probing is best-effort: the compressor can still run without it, but the UI
// needs duration, dimensions and the source bitrate to show an estimate and
// to warn when re-encoding is unlikely to help.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <Infrastructure/Result.h>

namespace vsr::transcode {

struct MediaInfo {
    std::int64_t durationMs = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    double frameRate = 0.0;
    std::string videoCodec;   // ffprobe codec_name, e.g. "h264"
    std::string audioCodec;   // empty when the file has no audio stream
    std::uint32_t audioChannels = 0;
    std::int64_t bitrateBps = 0;
    std::uint64_t fileBytes = 0;
    bool hasVideo = false;
    bool hasAudio = false;
};

// Pure parser over `ffprobe -print_format json -show_format -show_streams`.
Result<MediaInfo> ParseProbeJson(const std::string& json);

// Locates the bundled tool: VSR_<NAME>_PATH override, application directory,
// PATH, then C:\ffmpeg\bin.
Result<std::wstring> LocateFfprobe();
Result<std::wstring> LocateFfmpeg();

// "ffprobe -v error -print_format json -show_format -show_streams ..."
std::wstring BuildProbeArguments(const std::wstring& input);

Result<MediaInfo> ProbeFile(const std::wstring& ffprobePath,
                            const std::filesystem::path& input,
                            std::uint32_t timeoutMs = 20000);

// Size on disk, 0 when the file cannot be queried.
std::uint64_t FileSizeBytes(const std::filesystem::path& path) noexcept;

} // namespace vsr::transcode
