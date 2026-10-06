#include "Test.h"
int main() {
    unsigned failures = 0;
    if (test::cases().empty()) {
        std::cerr << "BLOCKED: zero tests\n";
        return 2;
    }
    for (auto& t : test::cases()) {
        std::cout << "RUN " << t.name << std::endl;
        try {
            t.run();
            std::cout << "PASS " << t.name << std::endl;
        } catch (const std::exception& e) {
            ++failures;
            std::cerr << "ASSERT_FAIL " << t.name << ": " << e.what() << std::endl;
        } catch (...) {
            ++failures;
            std::cerr << "ASSERT_FAIL " << t.name << " unknown exception" << std::endl;
        }
    }
    std::cout << test::cases().size() << " cases, " << failures << " failures\n";
    return failures ? 1 : 0;
}
