// Virtual Screen Recorder - injectable clocks (design.md §4.8 time model).
#pragma once

#include <chrono>
#include <cstdint>

#include <windows.h>

namespace vsr {

// Abstraction so timestamp math, file naming, and pause accounting are
// unit-testable without real time (design.md §8.1).
class IClock {
public:
    virtual ~IClock() = default;

    // Monotonic time since an arbitrary epoch. Never goes backwards.
    [[nodiscard]] virtual std::chrono::nanoseconds MonotonicNow() const = 0;

    // Wall-clock time in milliseconds since the Unix epoch (UTC).
    [[nodiscard]] virtual std::int64_t SystemTimeMs() const = 0;
};

// Production clock: QPC for monotonic time, UTC system time for wall clock.
class SystemClock final : public IClock {
public:
    SystemClock() noexcept;

    std::chrono::nanoseconds MonotonicNow() const override;
    std::int64_t SystemTimeMs() const override;

private:
    std::chrono::nanoseconds qpcBase_{};
    std::int64_t systemBaseMs_ = 0;
    double qpcToNs_ = 0.0;
};

// Test clock with manually advanced time.
class ManualClock final : public IClock {
public:
    explicit ManualClock(std::int64_t startMonotonicMs = 0, std::int64_t startSystemMs = 0) noexcept
        : monotonicNs_(startMonotonicMs * 1'000'000), systemMs_(startSystemMs) {}

    std::chrono::nanoseconds MonotonicNow() const override { return std::chrono::nanoseconds(monotonicNs_); }
    std::int64_t SystemTimeMs() const override { return systemMs_; }

    void AdvanceMonotonic(std::chrono::nanoseconds delta) noexcept { monotonicNs_ += delta.count(); }
    void SetSystemTimeMs(std::int64_t ms) noexcept { systemMs_ = ms; }

private:
    std::int64_t monotonicNs_;
    std::int64_t systemMs_;
};

} // namespace vsr
