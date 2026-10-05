#pragma once
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace test {
struct Case {
    std::string name;
    void (*run)();
};
inline std::vector<Case>& cases() {
    static std::vector<Case> r;
    return r;
}
struct Register {
    Register(const char* n, void (*f)()) {
        cases().push_back({n, f});
    }
};
inline void check(bool ok, const char* expression, const char* file, int line) {
    if (!ok)
        throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + " CHECK(" +
                                 expression + ")");
}
} // namespace test
#define CAT_(a, b) a##b
#define CAT(a, b) CAT_(a, b)
#define TEST(name)                                                                                 \
    static void CAT(test_, __LINE__)();                                                            \
    static test::Register CAT(reg_, __LINE__)(name, CAT(test_, __LINE__));                         \
    static void CAT(test_, __LINE__)()
#define CHECK(...) test::check(bool((__VA_ARGS__)), #__VA_ARGS__, __FILE__, __LINE__)
