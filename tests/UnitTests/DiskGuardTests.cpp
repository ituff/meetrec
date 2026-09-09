// Disk space guard tests (T060, AC-007): threshold policy and low-space stop.
#include "TestFramework.h"
#include <Application/DiskSpaceGuard.h>

#include <map>
#include <string>

using vsr::app::DiskSpaceGuard;

namespace {
std::wstring Key(const std::filesystem::path& p) { return p.wstring(); }
} // namespace

VSR_TEST(DiskGuard, ThresholdIsMaxOfReserveAndThirtySeconds) {
    // 6 Mbps => 30s = 22.5 MB, below the 256 MB floor.
    VSR_CHECK_EQ(DiskSpaceGuard::StopThresholdBytes(6'000'000),
                 DiskSpaceGuard::kMinimumReserveBytes);
    // 100 Mbps => 30s = 375 MB, above the floor.
    VSR_CHECK_EQ(DiskSpaceGuard::StopThresholdBytes(100'000'000),
                 static_cast<std::uintmax_t>(100'000'000.0 * 30.0 / 8));
}

VSR_TEST(DiskGuard, RequiredBytesConvertsBitrateAndTime) {
    VSR_CHECK_EQ(DiskSpaceGuard::RequiredBytes(8'000'000, 1.0),
                 static_cast<std::uintmax_t>(1'000'000)); // 8 Mbps = 1 MB/s
}

VSR_TEST(DiskGuard, StopsWhenFreeSpaceBelowThreshold) {
    using namespace std::placeholders;
    DiskSpaceGuard guard;
    const std::filesystem::path dir = L"C:\\fake";
    const std::uintmax_t threshold = DiskSpaceGuard::StopThresholdBytes(6'000'000);

    guard.Begin(dir, 6'000'000, [threshold](const std::filesystem::path&) {
        return threshold; // exactly at the threshold -> stop
    });
    auto atThreshold = guard.Check(0);
    VSR_CHECK(!atThreshold.IsOk());
    VSR_CHECK(atThreshold.GetError().code == vsr::ErrorCode::OutputDiskLow);

    guard.Begin(dir, 6'000'000, [threshold](const std::filesystem::path&) {
        return threshold + 1;
    });
    VSR_CHECK(guard.Check(0).IsOk());
}

VSR_TEST(DiskGuard, ProbeFailureIsReportedAsWriteError) {
    DiskSpaceGuard guard;
    guard.Begin(L"C:\\fake", 6'000'000, [](const std::filesystem::path&) {
        return static_cast<std::uintmax_t>(-1);
    });
    auto result = guard.Check(0);
    VSR_CHECK(!result.IsOk());
    VSR_CHECK(result.GetError().code == vsr::ErrorCode::OutputNotWritable);
}
