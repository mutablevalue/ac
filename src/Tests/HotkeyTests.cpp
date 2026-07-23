#include "TestHarness.hpp"

#include "Input/HotkeyManager.hpp"

#include <linux/input-event-codes.h>

namespace Autoclicker::Tests {

auto hotkey_tests() -> void {
    auto Manager = Input::HotkeyManager{};
    Manager.configure({KEY_F8, false, false, false, false},
                      {KEY_F12, true, false, true, false});
    require(Manager.process(KEY_F8, 1) == Types::HotkeyAction::ToggleAutoclicker,
            "F8 must toggle the autoclicker");
    require(!Manager.process(KEY_F8, 2), "key autorepeat must not trigger a binding");
    static_cast<void>(Manager.process(KEY_F8, 0));
    static_cast<void>(Manager.process(KEY_LEFTCTRL, 1));
    static_cast<void>(Manager.process(KEY_LEFTSHIFT, 1));
    require(Manager.process(KEY_F12, 1) == Types::HotkeyAction::ExitAutoclicker,
            "configured modifier chord must request exit");
    Manager.reset();
    require(!Manager.process(KEY_F12, 1), "reset must clear held modifiers");
}

} // namespace Autoclicker::Tests

