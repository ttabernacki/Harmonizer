#pragma once

#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <cmath>
#include <sstream>

// ============================================================================
// Minimal test framework — no external dependencies required.
//
// Usage:
//   TEST(MyTestName) { EXPECT_EQ(1 + 1, 2); }
//
// Output follows a googletest-like format for familiarity.
// ============================================================================

struct TestCase
{
    std::string name;
    std::function<void()> func;
};

inline std::vector<TestCase>& getTests()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failCount() { static int c = 0; return c; }
inline int& passCount() { static int c = 0; return c; }

struct TestRegistrar
{
    TestRegistrar(const char* name, std::function<void()> f)
    {
        getTests().push_back({name, std::move(f)});
    }
};

#define TEST(name)                                                \
    static void test_##name();                                    \
    static TestRegistrar reg_##name(#name, test_##name);          \
    static void test_##name()

#define EXPECT(cond) do {                                         \
    if (!(cond)) {                                                \
        std::cerr << "  FAIL: " << __FILE__ << ":" << __LINE__   \
                  << " - " << #cond << "\n";                      \
        ++failCount();                                            \
    } else {                                                      \
        ++passCount();                                            \
    }                                                             \
} while(0)

#define EXPECT_EQ(a, b) do {                                      \
    auto _a = (a); auto _b = (b);                                 \
    if (_a != _b) {                                               \
        std::cerr << "  FAIL: " << __FILE__ << ":" << __LINE__   \
                  << " - " << #a << " == " << #b                  \
                  << " (got " << _a << " vs " << _b << ")\n";    \
        ++failCount();                                            \
    } else {                                                      \
        ++passCount();                                            \
    }                                                             \
} while(0)

#define EXPECT_NE(a, b) do {                                      \
    auto _a = (a); auto _b = (b);                                 \
    if (_a == _b) {                                               \
        std::cerr << "  FAIL: " << __FILE__ << ":" << __LINE__   \
                  << " - " << #a << " != " << #b                  \
                  << " (both " << _a << ")\n";                    \
        ++failCount();                                            \
    } else {                                                      \
        ++passCount();                                            \
    }                                                             \
} while(0)

#define EXPECT_NEAR(a, b, tol) do {                               \
    auto _a = (a); auto _b = (b); auto _t = (tol);               \
    if (std::abs(_a - _b) > _t) {                                \
        std::cerr << "  FAIL: " << __FILE__ << ":" << __LINE__   \
                  << " - |" << #a << " - " << #b << "| <= "      \
                  << #tol << " (got " << _a << " vs " << _b      \
                  << ", diff=" << std::abs(_a - _b) << ")\n";    \
        ++failCount();                                            \
    } else {                                                      \
        ++passCount();                                            \
    }                                                             \
} while(0)

#define EXPECT_TRUE(cond)  EXPECT(cond)
#define EXPECT_FALSE(cond) EXPECT(!(cond))

#define EXPECT_GT(a, b) do {                                      \
    auto _a = (a); auto _b = (b);                                 \
    if (!(_a > _b)) {                                             \
        std::cerr << "  FAIL: " << __FILE__ << ":" << __LINE__   \
                  << " - " << #a << " > " << #b                   \
                  << " (got " << _a << " vs " << _b << ")\n";    \
        ++failCount();                                            \
    } else {                                                      \
        ++passCount();                                            \
    }                                                             \
} while(0)

#define EXPECT_GE(a, b) do {                                      \
    auto _a = (a); auto _b = (b);                                 \
    if (!(_a >= _b)) {                                            \
        std::cerr << "  FAIL: " << __FILE__ << ":" << __LINE__   \
                  << " - " << #a << " >= " << #b                  \
                  << " (got " << _a << " vs " << _b << ")\n";    \
        ++failCount();                                            \
    } else {                                                      \
        ++passCount();                                            \
    }                                                             \
} while(0)

#define EXPECT_LE(a, b) do {                                      \
    auto _a = (a); auto _b = (b);                                 \
    if (!(_a <= _b)) {                                            \
        std::cerr << "  FAIL: " << __FILE__ << ":" << __LINE__   \
                  << " - " << #a << " <= " << #b                  \
                  << " (got " << _a << " vs " << _b << ")\n";    \
        ++failCount();                                            \
    } else {                                                      \
        ++passCount();                                            \
    }                                                             \
} while(0)

#define EXPECT_LT(a, b) do {                                      \
    auto _a = (a); auto _b = (b);                                 \
    if (!(_a < _b)) {                                             \
        std::cerr << "  FAIL: " << __FILE__ << ":" << __LINE__   \
                  << " - " << #a << " < " << #b                   \
                  << " (got " << _a << " vs " << _b << ")\n";    \
        ++failCount();                                            \
    } else {                                                      \
        ++passCount();                                            \
    }                                                             \
} while(0)

inline int runAllTests()
{
    int testsPassed = 0, testsFailed = 0;
    for (auto& tc : getTests())
    {
        int prevFail = failCount();
        std::cout << "[ RUN      ] " << tc.name << "\n";
        try {
            tc.func();
        } catch (const std::exception& e) {
            std::cerr << "  EXCEPTION: " << e.what() << "\n";
            ++failCount();
        } catch (...) {
            std::cerr << "  EXCEPTION: (unknown)\n";
            ++failCount();
        }
        if (failCount() > prevFail) {
            std::cout << "[   FAILED ] " << tc.name << "\n";
            ++testsFailed;
        } else {
            std::cout << "[       OK ] " << tc.name << "\n";
            ++testsPassed;
        }
    }
    std::cout << "\n========================================\n";
    std::cout << passCount() << " assertions passed, " << failCount() << " failed\n";
    std::cout << testsPassed << " tests passed, " << testsFailed << " tests failed\n";
    if (testsFailed > 0)
        std::cout << "*** SOME TESTS FAILED ***\n";
    else
        std::cout << "All tests passed!\n";
    return testsFailed > 0 ? 1 : 0;
}
