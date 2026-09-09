// Virtual Screen Recorder - minimal unit-test framework (no dependencies).
#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace vsr::test {

struct TestCase {
    const char* suite;
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> registry;
    return registry;
}

inline int& Failures() {
    static int failures = 0;
    return failures;
}

inline bool Register(TestCase test) {
    Registry().push_back(std::move(test));
    return true;
}

inline int RunAll() {
    int ran = 0;
    for (const auto& t : Registry()) {
        const int before = Failures();
        try {
            t.fn();
        } catch (const std::exception& e) {
            std::cout << "[CRASH] " << t.suite << "." << t.name << ": " << e.what() << "\n";
            ++Failures();
        } catch (...) {
            std::cout << "[CRASH] " << t.suite << "." << t.name << ": unknown exception\n";
            ++Failures();
        }
        if (Failures() == before) {
            std::cout << "[PASS] " << t.suite << "." << t.name << "\n";
        }
        ++ran;
    }
    std::cout << "\n" << ran << " tests, " << Failures() << " failing checks\n";
    return Failures() == 0 ? 0 : 1;
}

} // namespace vsr::test

#define VSR_TEST(suite, name)                                                        \
    static void suite##_##name##_body();                                            \
    static const bool suite##_##name##_registered =                                 \
        ::vsr::test::Register({#suite, #name, &suite##_##name##_body});             \
    static void suite##_##name##_body()

#define VSR_CHECK(cond)                                                              \
    do {                                                                             \
        if (!(cond)) {                                                               \
            ::vsr::test::Failures()++;                                               \
            std::cout << "  [FAIL] " << __FILE__ << ":" << __LINE__ << ": " #cond    \
                      << "\n";                                                       \
        }                                                                            \
    } while (0)

#define VSR_CHECK_EQ(a, b)                                                           \
    do {                                                                             \
        auto va = (a);                                                               \
        auto vb = (b);                                                               \
        if (!(va == vb)) {                                                           \
            ::vsr::test::Failures()++;                                               \
            std::cout << "  [FAIL] " << __FILE__ << ":" << __LINE__ << ": " #a       \
                      << " == " #b << "\n";                                          \
        }                                                                            \
    } while (0)
