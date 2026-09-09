// Virtual Screen Recorder - minimal expected-like Result type (design.md §6).
#pragma once

#include "Error.h"
#include <cassert>
#include <optional>
#include <utility>

namespace vsr {

// Holds either a value of T or an Error. Expected errors are part of the type
// signature at module boundaries; exceptions are reserved for unexpected
// construction failures (design.md §6). T need not be default-constructible
// (std::optional storage).
template <typename T>
class [[nodiscard]] Result {
public:
    Result(T value) : ok_(true), value_(std::move(value)) {}
    Result(Error error) : ok_(false), error_(std::move(error)) {}

    [[nodiscard]] bool IsOk() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    T& Value() noexcept { assert(ok_); return *value_; }
    const T& Value() const noexcept { assert(ok_); return *value_; }

    const Error& GetError() const noexcept { assert(!ok_); return error_; }
    Error TakeError() noexcept { assert(!ok_); return std::move(error_); }

private:
    bool ok_;
    std::optional<T> value_;
    Error error_{};
};

// Specialization for operations that produce no value.
template <>
class [[nodiscard]] Result<void> {
public:
    Result() : ok_(true) {}
    Result(Error error) : ok_(false), error_(std::move(error)) {}

    [[nodiscard]] bool IsOk() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    void Value() const noexcept { assert(ok_); }

    const Error& GetError() const noexcept { assert(!ok_); return error_; }
    Error TakeError() noexcept { assert(!ok_); return std::move(error_); }

private:
    bool ok_;
    Error error_{};
};

} // namespace vsr
