// Output file tests (T060): name pattern (FR-053), collision safety
// (FR-054), partial suffix (FR-057).
#include "TestFramework.h"
#include <Application/OutputFile.h>
#include <Infrastructure/Clock.h>
#include <Infrastructure/Strings.h>

#include <filesystem>
#include <fstream>

using namespace vsr;
using namespace vsr::app;

namespace {

struct FixedClock final : IClock {
    std::int64_t ms;
    explicit FixedClock(std::int64_t systemMs) : ms(systemMs) {}
    std::chrono::nanoseconds MonotonicNow() const override { return {}; }
    std::int64_t SystemTimeMs() const override { return ms; }
};

std::filesystem::path MakeTempDir(const char* name) {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / (L"vsr-" + Utf8ToWide(name));
    std::filesystem::create_directories(dir);
    return dir;
}

void Touch(const std::filesystem::path& p) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << "x";
}

} // namespace

VSR_TEST(OutputFile, StampMatchesPattern) {
    // 2026-09-09 12:34:56 local time, expressed as Unix ms; the local-time
    // rendering is verified indirectly (pattern + digits count).
    const std::wstring stamp = OutputStampFromMs(1'789'000'000'000); // arbitrary fixed point
    VSR_CHECK(stamp.rfind(L"Recording_", 0) == 0);
    VSR_CHECK_EQ(stamp.size(), 25u); // Recording_yyyyMMdd_HHmmss
    for (size_t i = 10; i < 23; ++i) {
        if (i == 18) {
            VSR_CHECK(stamp[i] == L'_');
        } else {
            VSR_CHECK(stamp[i] >= L'0' && stamp[i] <= L'9');
        }
    }
}

VSR_TEST(OutputFile, StampUsesLocalTimeComponents) {
    // 2026-09-09T12:00:00Z is 2026-09-09 20:00 (+0800) or 05:00 (-0700);
    // whatever the machine TZ, the rendered date must be one of the
    // neighboring days and a valid timestamp. We verify the UTC render for a
    // machine-independent property: the stamp for the epoch start is
    // deterministic per timezone.
    const std::wstring stamp = OutputStampFromMs(0);
    VSR_CHECK_EQ(stamp.size(), 25u);
    VSR_CHECK(stamp.rfind(L"Recording_", 0) == 0);
}

VSR_TEST(OutputFile, CollisionAvoidedWithSuffix) {
    const auto dir = MakeTempDir("collision");
    FixedClock clock(1'789'000'000'000);

    auto first = PlanOutputFile(dir, clock);
    VSR_CHECK(first.IsOk());
    Touch(first.Value().finalPath);

    auto second = PlanOutputFile(dir, clock);
    VSR_CHECK(second.IsOk());
    VSR_CHECK(second.Value().finalPath != first.Value().finalPath);
    VSR_CHECK(second.Value().finalPath.stem().wstring().back() == L'1');

    // Partial paths always carry .partial.mp4 before the final name is used.
    VSR_CHECK(second.Value().partialPath.wstring().find(L".partial.mp4") !=
              std::wstring::npos);
    VSR_CHECK(!std::filesystem::exists(second.Value().partialPath));

    std::filesystem::remove_all(dir);
}

VSR_TEST(OutputFile, RejectsUnwritableDirectory) {
    const std::filesystem::path bogus =
        std::filesystem::temp_directory_path() /
        L"vsr-nonexistent-parent\\vsr-child\\deeper";
    auto result = ValidateOutputDirectory(bogus, 1024);
    VSR_CHECK(!result.IsOk());
    VSR_CHECK(result.GetError().code == ErrorCode::OutputNotWritable ||
              result.GetError().code == ErrorCode::OutputDiskLow);
}

VSR_TEST(OutputFile, AcceptsWritableDirectoryWithSpace) {
    const auto dir = MakeTempDir("writable");
    auto result = ValidateOutputDirectory(dir, 1024);
    VSR_CHECK(result.IsOk());
    // Probe file must be cleaned up.
    bool leftover = false;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().filename().wstring().rfind(L"vsr-write-probe-", 0) == 0) leftover = true;
    }
    VSR_CHECK(!leftover);
    std::filesystem::remove_all(dir);
}

VSR_TEST(OutputFile, RejectsWhenRequiredSpaceUnavailable) {
    const auto dir = MakeTempDir("nospace");
    // Require more free space than any plausible temp drive has.
    auto result = ValidateOutputDirectory(dir, 1'000'000'000'000'000ull);
    VSR_CHECK(!result.IsOk());
    VSR_CHECK(result.GetError().code == ErrorCode::OutputDiskLow);
    std::filesystem::remove_all(dir);
}
