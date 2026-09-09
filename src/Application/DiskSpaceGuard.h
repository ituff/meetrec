// Virtual Screen Recorder - disk-space guard (FR-059, FR-060, AC-007; T060).
//
// Threshold policy (documented): recording stops safely when free space drops
// below max(256 MB, 30 seconds of video at the active bitrate). Space is
// re-checked on a fixed poll interval during recording.
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>

#include <Infrastructure/Error.h>
#include <Infrastructure/Result.h>

namespace vsr::app {

class DiskSpaceGuard {
public:
    // Injectable free-space probe for unit tests. Default queries the volume.
    using FreeSpaceFn = std::function<std::uintmax_t(const std::filesystem::path&)>;

    static constexpr std::uintmax_t kMinimumReserveBytes = 256ull * 1024 * 1024;
    static constexpr std::chrono::seconds kPollInterval{5};

    DiskSpaceGuard();

    // Configures the guard for a recording run.
    void Begin(const std::filesystem::path& outputDir, std::uintmax_t bitrateBps,
               FreeSpaceFn probe = nullptr);

    // Returns an error when the disk is critically low (FR-060). Cheap; call
    // on the poll cadence from the recording worker.
    Result<void> Check(std::uintmax_t encodedBytesSoFar);

    // Bytes required for a recording of `seconds` at `bitrateBps` plus the
    // safety reserve.
    static std::uintmax_t RequiredBytes(std::uintmax_t bitrateBps, double seconds);
    static std::uintmax_t StopThresholdBytes(std::uintmax_t bitrateBps);

private:
    std::filesystem::path dir_;
    std::uintmax_t thresholdBytes_ = kMinimumReserveBytes;
    FreeSpaceFn probe_;
};

} // namespace vsr::app
