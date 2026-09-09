#include "DiskSpaceGuard.h"

namespace vsr::app {

DiskSpaceGuard::DiskSpaceGuard() = default;

std::uintmax_t DiskSpaceGuard::RequiredBytes(std::uintmax_t bitrateBps, double seconds) {
    return static_cast<std::uintmax_t>(static_cast<double>(bitrateBps) * seconds) / 8;
}

std::uintmax_t DiskSpaceGuard::StopThresholdBytes(std::uintmax_t bitrateBps) {
    const std::uintmax_t ThirtySeconds = RequiredBytes(bitrateBps, 30.0);
    return std::max(kMinimumReserveBytes, ThirtySeconds);
}

void DiskSpaceGuard::Begin(const std::filesystem::path& outputDir, std::uintmax_t bitrateBps,
                           FreeSpaceFn probe) {
    dir_ = outputDir;
    thresholdBytes_ = StopThresholdBytes(bitrateBps);
    probe_ = std::move(probe);
}

Result<void> DiskSpaceGuard::Check(std::uintmax_t encodedBytesSoFar) {
    std::uintmax_t freeBytes = 0;
    if (probe_) {
        freeBytes = probe_(dir_);
        if (freeBytes == static_cast<std::uintmax_t>(-1)) {
            return Error(ErrorCode::OutputNotWritable,
                         L"The output drive could not be read; recording stopped safely.",
                         L"Injected free-space probe failed", true);
        }
    } else {
        ULARGE_INTEGER freeToCaller{};
        if (!GetDiskFreeSpaceExW(dir_.c_str(), &freeToCaller, nullptr, nullptr)) {
            return ErrorFromWin32(GetLastError(), ErrorCode::OutputNotWritable,
                                  L"The output drive could not be read; recording stopped safely.",
                                  L"GetDiskFreeSpaceExW failed during recording", true);
        }
        freeBytes = freeToCaller.QuadPart;
    }

    // The partial file already occupies space on the volume; the meaningful
    // test is whether the volume can absorb the next stretch of video after
    // what we have already written. Free space queried from the volume already
    // accounts for written bytes, so compare directly against the threshold.
    UNREFERENCED_PARAMETER(encodedBytesSoFar);
    if (freeBytes <= thresholdBytes_) {
        return ErrorFromWin32(ERROR_DISK_FULL, ErrorCode::OutputDiskLow,
                              L"Disk space is critically low. The recording was stopped and "
                              L"saved as-is.",
                              L"Free bytes at or below the stop threshold", true);
    }
    return Result<void>{};
}

} // namespace vsr::app
