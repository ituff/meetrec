// MeetRec - semantic version parsing/comparison for the updater.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace vsr::update {

struct SemanticVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;

    // Accepts "1.2.3" and "v1.2.3"; a fourth numeric component is ignored.
    // Pre-release suffixes (-beta.1) are rejected so prereleases never
    // trigger an update.
    static std::optional<SemanticVersion> Parse(std::string_view text);

    std::string ToString() const;
};

bool operator==(const SemanticVersion& a, const SemanticVersion& b) noexcept;
bool operator!=(const SemanticVersion& a, const SemanticVersion& b) noexcept;
bool operator<(const SemanticVersion& a, const SemanticVersion& b) noexcept;
bool operator>(const SemanticVersion& a, const SemanticVersion& b) noexcept;

} // namespace vsr::update
