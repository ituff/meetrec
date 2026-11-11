// MeetRec - GitHub release metadata for the updater.
#pragma once

#include <cstdint>
#include <string>

#include <Infrastructure/Result.h>

#include "SemanticVersion.h"

namespace vsr::update {

struct ReleaseInfo {
    SemanticVersion version;
    std::string tagName;
    std::wstring htmlUrl;
    std::wstring zipName;
    std::wstring zipUrl;
    std::wstring sumsUrl;
    std::uint64_t zipSize = 0;
};

struct UpdateCheck {
    bool available = false;
    ReleaseInfo release;
};

// Parses `GET /repos/{owner}/{repo}/releases/latest`. The release must be a
// stable version with both the portable zip and SHA256SUMS.txt assets.
Result<ReleaseInfo> ParseLatestReleaseJson(const std::string& json);

} // namespace vsr::update
