#pragma once

#include "Types/ConfigurationTypes.hpp"
#include "Types/InputTypes.hpp"

#include <array>
#include <optional>

namespace Autoclicker::Input {

class HotkeyManager final {
public:
    auto configure(Types::KeyChord ToggleLeftBinding, Types::KeyChord ToggleRightBinding,
                   Types::KeyChord ExitBinding) -> void;
    [[nodiscard]] auto process(std::uint16_t Code, std::int32_t Value)
        -> std::optional<Types::HotkeyAction>;
    // Tracks held state exactly like process, but reports the pressed chord instead of
    // dispatching it. Modifier presses only update state; they never complete a capture.
    [[nodiscard]] auto capture(std::uint16_t Code, std::int32_t Value)
        -> std::optional<Types::KeyChord>;
    auto reset() -> void;

private:
    [[nodiscard]] auto held_modifiers() const -> Types::KeyChord;
    [[nodiscard]] auto matches(const Types::KeyChord& Binding, std::uint16_t Code) const -> bool;

    static constexpr auto KeyCount = std::size_t{768};
    std::array<bool, KeyCount> Pressed{};
    Types::KeyChord ToggleLeftBinding;
    Types::KeyChord ToggleRightBinding;
    Types::KeyChord ExitBinding;
};

} // namespace Autoclicker::Input

