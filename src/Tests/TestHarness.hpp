#pragma once

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace Autoclicker::Tests {

inline auto require(const bool Condition, const std::string_view Message) -> void {
    if (!Condition) throw std::runtime_error{std::string{Message}};
}

auto configuration_tests() -> void;
auto hotkey_tests() -> void;
auto scheduler_tests() -> void;
auto protocol_tests() -> void;

} // namespace Autoclicker::Tests
