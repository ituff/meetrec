// Virtual Screen Recorder - output file naming (FR-053, FR-054, FR-057,
// FR-058; T060).
//
// Default name: Recording_yyyyMMdd_HHmmss.mp4 (local time). Existing files
// are never overwritten without authorization; a numbered suffix
// (Recording_..._1.mp4) is chosen instead. Recordings are written to
// `<name>.partial.mp4` until finalization succeeds.
#pragma once

#include <filesystem>
#include <string>

#include <Infrastructure/Clock.h>
#include <Infrastructure/Error.h>
#include <Infrastructure/Result.h>

namespace vsr::app {

struct OutputFilePlan {
    std::filesystem::path finalPath;   // Recording_yyyyMMdd_HHmmss.mp4
    std::filesystem::path partialPath; // Recording_yyyyMMdd_HHmmss.partial.mp4
};

// Builds the stamp from a wall-clock time (pure; unit-tested).
std::wstring OutputStampFromMs(std::int64_t systemMs);

// Plans the output paths for a recording. `allowOverwrite` is always false in
// the MVP; collisions resolve by appending _1, _2, ... before the extension.
// `directory` must exist; emptiness falls back to the user's Videos folder.
Result<OutputFilePlan> PlanOutputFile(const std::filesystem::path& directory, IClock& clock);

// Validates an output directory: exists (or creatable), writable, and has at
// least `requiredBytes` free (FR-059). Returns typed errors for the UI.
Result<void> ValidateOutputDirectory(const std::filesystem::path& directory,
                                     std::uintmax_t requiredBytes);

} // namespace vsr::app
