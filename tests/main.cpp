#include "test.hpp"

#include <exception>
#include <string_view>

int main(int argc, char** argv) {
    const std::string_view filter = argc > 1 ? argv[1] : "";
    int run = 0;
    for (const auto& test : studio::test::registry()) {
        if (!filter.empty() && std::string_view(test.name).find(filter) == std::string_view::npos) {
            continue;
        }
        ++run;
        const int before = studio::test::failures();
        try {
            test.body();
        } catch (const std::exception& error) {
            ++studio::test::failures();
            std::cerr << test.name << ": threw: " << error.what() << '\n';
        }
        std::cout << (studio::test::failures() == before ? "pass " : "FAIL ") << test.name << '\n';
    }
    std::cout << run << " tests, " << studio::test::failures() << " failures\n";
    return studio::test::failures() == 0 && run > 0 ? 0 : 1;
}
