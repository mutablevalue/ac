#pragma once

#include "Types/ConfigurationTypes.hpp"
#include "Types/InputTypes.hpp"

#include <array>
#include <optional>

namespace Autoclicker::Input {

class HotkeyManager final {
public:
    auto configure(Types::KeyChord ToggleBinding, Types::KeyChord ExitBinding) -> void;
    [[nodiscard]] auto process(std::uint16_t Code, std::int32_t Value)
        -> std::optional<Types::HotkeyAction>;
    auto reset() -> void;

private:
    [[nodiscard]] auto matches(const Types::KeyChord& Binding, std::uint16_t Code) const -> bool;

    static constexpr auto KeyCount = std::size_t{768};
    std::array<bool, KeyCount> Pressed{};
    Types::KeyChord ToggleBinding;
    Types::KeyChord ExitBinding;
};

} // namespace Autoclicker::Input

