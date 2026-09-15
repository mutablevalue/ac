#pragma once

#include "Types/ConfigurationTypes.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Autoclicker::Utils {

// Translates raw evdev codes into text for both processes. The GUI cannot link libevdev, so
// libevdev_event_code_get_name is unavailable there; this table is compiled into the shared
// library instead and needs only the UAPI header for its constants.
class KeyNames final {
public:
    struct Entry final {
        std::uint16_t Code{};
        std::string_view Name;
    };

    [[nodiscard]] static auto table() noexcept -> std::span<const Entry>;

    // Falls back to "Key <n>" / "Mouse button <n>" for codes outside the table.
    [[nodiscard]] static auto display_name(std::uint16_t Code) -> std::string;
    [[nodiscard]] static auto chord_name(const Types::KeyChord& Value) -> std::string;
    [[nodiscard]] static auto is_mouse_button(std::uint16_t Code) noexcept -> bool;
    [[nodiscard]] static auto is_modifier(std::uint16_t Code) noexcept -> bool;
};

} // namespace Autoclicker::Utils
