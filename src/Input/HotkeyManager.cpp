#include "Input/HotkeyManager.hpp"

#include <linux/input-event-codes.h>

namespace Autoclicker::Input {

auto HotkeyManager::configure(Types::KeyChord Toggle, Types::KeyChord Exit) -> void {
    ToggleBinding = Toggle;
    ExitBinding = Exit;
    reset();
}

auto HotkeyManager::matches(const Types::KeyChord& Binding, const std::uint16_t Code) const -> bool {
    const auto Held = [this](const std::size_t Key) { return Key < Pressed.size() && Pressed[Key]; };
    const auto Control = Held(KEY_LEFTCTRL) || Held(KEY_RIGHTCTRL);
    const auto Alt = Held(KEY_LEFTALT) || Held(KEY_RIGHTALT);
    const auto Shift = Held(KEY_LEFTSHIFT) || Held(KEY_RIGHTSHIFT);
    const auto Super = Held(KEY_LEFTMETA) || Held(KEY_RIGHTMETA);
    return Binding.KeyCode == Code && Binding.Control == Control && Binding.Alt == Alt &&
           Binding.Shift == Shift && Binding.Super == Super;
}

auto HotkeyManager::process(const std::uint16_t Code, const std::int32_t Value)
    -> std::optional<Types::HotkeyAction> {
    if (Code >= Pressed.size()) return std::nullopt;
    if (Value == 2) return std::nullopt;
    Pressed[Code] = Value == 1;
    if (Value != 1) return std::nullopt;
    if (matches(ExitBinding, Code)) return Types::HotkeyAction::ExitAutoclicker;
    if (matches(ToggleBinding, Code)) return Types::HotkeyAction::ToggleAutoclicker;
    return std::nullopt;
}

auto HotkeyManager::reset() -> void { Pressed.fill(false); }

} // namespace Autoclicker::Input
