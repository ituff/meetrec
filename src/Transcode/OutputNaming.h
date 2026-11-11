// MeetRec - output naming for the compressor (ADR-007).
//
// Compressed files are written next to the source by default and never
// overwrite an existing file (FR-054 applies to the compressor as well).
#pragma once

#include <filesystem>

namespace vsr::transcode {

// `<dir>/<stem>-min.mp4`; `outputDir` empty means the input's directory.
std::filesystem::path DeriveOutputPath(const std::filesystem::path& input,
                                       const std::filesystem::path& outputDir);

// Adds -2, -3, ... before the extension until the path does not exist.
std::filesystem::path EnsureUniquePath(const std::filesystem::path& desired);

// The in-progress file the encoder writes before the atomic rename
// (mirrors the recorder's .partial.mp4 convention).
std::filesystem::path PartialPathFor(const std::filesystem::path& output);

} // namespace vsr::transcode
