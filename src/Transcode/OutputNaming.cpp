#include "OutputNaming.h"

#include <string>

namespace vsr::transcode {

namespace {

std::filesystem::path UniqueCandidate(const std::filesystem::path& desired, int attempt) {
    if (attempt <= 1) return desired;
    const std::wstring stem = desired.stem().wstring();
    const std::wstring extension = desired.extension().wstring();
    return desired.parent_path() /
           (stem + L"-" + std::to_wstring(attempt) + extension);
}

bool Exists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

} // namespace

std::filesystem::path DeriveOutputPath(const std::filesystem::path& input,
                                       const std::filesystem::path& outputDir) {
    std::filesystem::path directory = outputDir;
    if (directory.empty()) directory = input.parent_path();
    return directory / (input.stem().wstring() + L"-min.mp4");
}

std::filesystem::path EnsureUniquePath(const std::filesystem::path& desired) {
    for (int attempt = 1; attempt < 1000; ++attempt) {
        const std::filesystem::path candidate = UniqueCandidate(desired, attempt);
        if (!Exists(candidate)) return candidate;
    }
    // Pathological directory: fall back to a numeric suffix from the clock.
    return UniqueCandidate(desired,
                           static_cast<int>(std::filesystem::file_time_type::clock::now()
                                                .time_since_epoch()
                                                .count() &
                                            0x7FFF));
}

std::filesystem::path PartialPathFor(const std::filesystem::path& output) {
    std::filesystem::path partial = output;
    partial.replace_extension(L".partial.mp4");
    return partial;
}

} // namespace vsr::transcode
