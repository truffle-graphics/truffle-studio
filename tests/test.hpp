#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

// Minimal self-registering test harness for Studio's non-GUI libraries.
namespace studio::test {

struct Case {
    const char* name;
    std::function<void()> body;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Register {
    Register(const char* name, std::function<void()> body) {
        registry().push_back({name, std::move(body)});
    }
};

inline void check(bool value, const char* expression, const char* file, int line) {
    if (!value) {
        ++failures();
        std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
    }
}

inline bool near(double a, double b, double tolerance = 1e-6) {
    return std::abs(a - b) <= tolerance * (1.0 + std::abs(a) + std::abs(b));
}

} // namespace studio::test

#define TFLST_CONCAT2(a, b) a##b
#define TFLST_CONCAT(a, b) TFLST_CONCAT2(a, b)
#define TEST(name)                                                                       \
    static void TFLST_CONCAT(test_, name)();                                            \
    static const ::studio::test::Register TFLST_CONCAT(register_, name)(               \
        #name, &TFLST_CONCAT(test_, name));                                             \
    static void TFLST_CONCAT(test_, name)()
#define CHECK(expression) ::studio::test::check((expression), #expression, __FILE__, __LINE__)
#define CHECK_NEAR(a, b) CHECK(::studio::test::near((a), (b)))
