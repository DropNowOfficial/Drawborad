#include "Test.h"
int main() {
    unsigned failures = 0;
    if (test::cases().empty()) {
        std::cerr << "BLOCKED: zero tests\n";
        return 2;
    }
    for (auto& t : test::cases()) {
        try {
            t.run();
            std::cout << "PASS " << t.name << '\n';
        } catch (const std::exception& e) {
            ++failures;
            std::cerr << "ASSERT_FAIL " << t.name << ": " << e.what() << '\n';
        } catch (...) {
            ++failures;
            std::cerr << "ASSERT_FAIL " << t.name << " unknown exception\n";
        }
    }
    std::cout << test::cases().size() << " cases, " << failures << " failures\n";
    return failures ? 1 : 0;
}
