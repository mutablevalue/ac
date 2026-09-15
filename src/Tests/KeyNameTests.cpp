#include "TestHarness.hpp"

#include "Utils/KeyNames.hpp"

#include <algorithm>
#include <linux/input-event-codes.h>

namespace Autoclicker::Tests {

auto key_name_tests() -> void {
    using Utils::KeyNames;

    // Lookup binary searches the table, so an out-of-order edit would silently break naming.
    const auto Table = KeyNames::table();
    require(!Table.empty(), "the key name table must not be empty");
    require(std::ranges::is_sorted(Table, {}, &KeyNames::Entry::Code),
            "the key name table must stay sorted by code");
    require(std::ranges::adjacent_find(Table, {}, &KeyNames::Entry::Code) == Table.end(),
            "the key name table must not contain duplicate codes");

    require(KeyNames::display_name(KEY_F8) == "F8", "function keys must be named");
    require(KeyNames::display_name(KEY_A) == "A", "letter keys must be named");
    require(KeyNames::display_name(BTN_LEFT) == "Left Mouse Button", "mouse buttons must be named");
    require(KeyNames::display_name(BTN_SIDE) == "Mouse Side Button", "side buttons must be named");
    require(!KeyNames::display_name(KEY_MAX - 1).empty(), "an unknown code must still produce a name");

    require(KeyNames::chord_name({KEY_F12, true, false, true, false}) == "Ctrl+Shift+F12",
            "a chord must render its modifiers in a stable order");
    require(KeyNames::chord_name({KEY_F8, false, false, false, false}) == "F8",
            "an unmodified chord must render as the bare key name");

    require(KeyNames::is_mouse_button(BTN_LEFT) && KeyNames::is_mouse_button(BTN_TASK),
            "the mouse button range must cover BTN_MISC through BTN_TASK");
    require(!KeyNames::is_mouse_button(KEY_A), "letter keys are not mouse buttons");

    require(KeyNames::is_modifier(KEY_LEFTCTRL) && KeyNames::is_modifier(KEY_RIGHTMETA),
            "both sides of every modifier must be recognised");
    require(!KeyNames::is_modifier(KEY_F8), "function keys are not modifiers");
}

} // namespace Autoclicker::Tests
