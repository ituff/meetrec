#include "SemanticVersion.h"

#include <vector>

namespace vsr::update {

namespace {

std::optional<int> ParseComponent(std::string_view text) {
    if (text.empty()) return std::nullopt;
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        if (value > 100000) return std::nullopt;
        value = value * 10 + (c - '0');
    }
    return value;
}

} // namespace

std::optional<SemanticVersion> SemanticVersion::Parse(std::string_view text) {
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) {
        text.remove_prefix(1);
    }
    if (text.empty()) return std::nullopt;
    // Reject prerelease/build metadata: only stable releases update clients.
    if (text.find('-') != std::string_view::npos ||
        text.find('+') != std::string_view::npos) {
        return std::nullopt;
    }

    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t dot = text.find('.', start);
        const std::size_t end = dot == std::string_view::npos ? text.size() : dot;
        parts.push_back(text.substr(start, end - start));
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    if (parts.size() < 3 || parts.size() > 4) return std::nullopt;

    SemanticVersion version;
    const auto major = ParseComponent(parts[0]);
    const auto minor = ParseComponent(parts[1]);
    const auto patch = ParseComponent(parts[2]);
    if (!major || !minor || !patch) return std::nullopt;
    version.major = *major;
    version.minor = *minor;
    version.patch = *patch;
    return version;
}

std::string SemanticVersion::ToString() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." +
           std::to_string(patch);
}

bool operator==(const SemanticVersion& a, const SemanticVersion& b) noexcept {
    return a.major == b.major && a.minor == b.minor && a.patch == b.patch;
}

bool operator!=(const SemanticVersion& a, const SemanticVersion& b) noexcept {
    return !(a == b);
}

bool operator<(const SemanticVersion& a, const SemanticVersion& b) noexcept {
    if (a.major != b.major) return a.major < b.major;
    if (a.minor != b.minor) return a.minor < b.minor;
    return a.patch < b.patch;
}

bool operator>(const SemanticVersion& a, const SemanticVersion& b) noexcept {
    return b < a;
}

} // namespace vsr::update
