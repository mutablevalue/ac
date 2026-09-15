#include "Input/HotkeyManager.hpp"

#include "Utils/KeyNames.hpp"

#include <linux/input-event-codes.h>

namespace Autoclicker::Input {

auto HotkeyManager::configure(Types::KeyChord ToggleLeft, Types::KeyChord ToggleRight,
                              Types::KeyChord Exit) -> void {
    ToggleLeftBinding = ToggleLeft;
    ToggleRightBinding = ToggleRight;
    ExitBinding = Exit;
    reset();
}

auto HotkeyManager::held_modifiers() const -> Types::KeyChord {
    const auto Held = [this](const std::size_t Key) { return Key < Pressed.size() && Pressed[Key]; };
    return {.KeyCode = 0,
            .Control = Held(KEY_LEFTCTRL) || Held(KEY_RIGHTCTRL),
            .Alt = Held(KEY_LEFTALT) || Held(KEY_RIGHTALT),
            .Shift = Held(KEY_LEFTSHIFT) || Held(KEY_RIGHTSHIFT),
            .Super = Held(KEY_LEFTMETA) || Held(KEY_RIGHTMETA)};
}

auto HotkeyManager::matches(const Types::KeyChord& Binding, const std::uint16_t Code) const -> bool {
    auto Chord = held_modifiers();
    Chord.KeyCode = Code;
    return Binding == Chord;
}

auto HotkeyManager::process(const std::uint16_t Code, const std::int32_t Value)
    -> std::optional<Types::HotkeyAction> {
    if (Code >= Pressed.size()) return std::nullopt;
    if (Value == 2) return std::nullopt;
    Pressed[Code] = Value == 1;
    if (Value != 1) return std::nullopt;
    if (matches(ExitBinding, Code)) return Types::HotkeyAction::ExitAutoclicker;
    if (matches(ToggleLeftBinding, Code)) return Types::HotkeyAction::ToggleLeftClicker;
    if (matches(ToggleRightBinding, Code)) return Types::HotkeyAction::ToggleRightClicker;
    return std::nullopt;
}

auto HotkeyManager::capture(const std::uint16_t Code, const std::int32_t Value)
    -> std::optional<Types::KeyChord> {
    if (Code >= Pressed.size()) return std::nullopt;
    if (Value == 2) return std::nullopt;
    Pressed[Code] = Value == 1;
    if (Value != 1) return std::nullopt;
    // A modifier alone is not a binding; it only contributes to the next real key.
    if (Utils::KeyNames::is_modifier(Code)) return std::nullopt;
    auto Chord = held_modifiers();
    Chord.KeyCode = Code;
    return Chord;
}

auto HotkeyManager::reset() -> void { Pressed.fill(false); }

} // namespace Autoclicker::Input
