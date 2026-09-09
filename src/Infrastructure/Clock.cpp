#include "Clock.h"

namespace vsr {

SystemClock::SystemClock() noexcept {
    LARGE_INTEGER freq{};
    LARGE_INTEGER counter{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    qpcToNs_ = 1'000'000'000.0 / static_cast<double>(freq.QuadPart);
    qpcBase_ = std::chrono::nanoseconds(static_cast<std::int64_t>(
        static_cast<double>(counter.QuadPart) * qpcToNs_));
    systemBaseMs_ = SystemTimeMs();
}

std::chrono::nanoseconds SystemClock::MonotonicNow() const {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const auto now = std::chrono::nanoseconds(static_cast<std::int64_t>(
        static_cast<double>(counter.QuadPart) * qpcToNs_));
    return now - qpcBase_;
}

std::int64_t SystemClock::SystemTimeMs() const {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    // FILETIME is 100ns intervals since 1601-01-01; convert to Unix epoch ms.
    constexpr std::int64_t kFileTimeToUnixEpoch100Ns = 116'444'736'000'000'000LL;
    const std::int64_t t100ns =
        (static_cast<std::int64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return (t100ns - kFileTimeToUnixEpoch100Ns) / 10'000;
}

} // namespace vsr
