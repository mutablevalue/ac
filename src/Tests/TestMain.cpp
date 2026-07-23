#include "TestHarness.hpp"

#include <exception>

auto main() -> int {
    try {
        Autoclicker::Tests::configuration_tests();
        Autoclicker::Tests::hotkey_tests();
        Autoclicker::Tests::scheduler_tests();
        Autoclicker::Tests::protocol_tests();
        std::cout << "All FastClicker tests passed\n";
        return 0;
    } catch (const std::exception& Exception) {
        std::cerr << "Test failure: " << Exception.what() << '\n';
        return 1;
    }
}
