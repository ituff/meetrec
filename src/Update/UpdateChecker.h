// MeetRec - GitHub release checking and verified update downloads.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <Infrastructure/Result.h>

#include "Http.h"
#include "ReleaseInfo.h"

namespace vsr::update {

// Overridden by VSR_UPDATE_API_URL for local mock/qualification servers.
std::wstring LatestReleaseUrl();

// Compares the latest stable release with `currentVersion` (e.g. "1.2.0").
Result<UpdateCheck> CheckForUpdate(const std::string& currentVersion);

// Finds the lowercase SHA-256 hex digest for `fileName` in a SHA256SUMS.txt
// body ("<hash>  <name>" lines, optional leading '*' on the name).
std::optional<std::string> FindChecksum(const std::string& sumsText,
                                        const std::wstring& fileName);

// Downloads the release zip next to `destinationDir`, verifies it against the
// release's SHA256SUMS.txt, and returns the local path. A cached zip with a
// matching hash is reused.
Result<std::filesystem::path> DownloadVerified(const ReleaseInfo& release,
                                               const std::filesystem::path& destinationDir,
                                               const std::atomic<bool>& cancel,
                                               const DownloadProgress& onProgress);

Result<std::string> Sha256File(const std::filesystem::path& path);

} // namespace vsr::update
