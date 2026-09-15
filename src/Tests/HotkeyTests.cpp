#include "TestHarness.hpp"

#include "Input/HotkeyManager.hpp"

#include <linux/input-event-codes.h>

namespace Autoclicker::Tests {

auto hotkey_tests() -> void {
    auto Manager = Input::HotkeyManager{};
    Manager.configure({KEY_F8, false, false, false, false},
                      {KEY_F9, false, false, false, false},
                      {KEY_F12, true, false, true, false});
    require(Manager.process(KEY_F8, 1) == Types::HotkeyAction::ToggleLeftClicker,
            "F8 must toggle the left clicker");
    require(!Manager.process(KEY_F8, 2), "key autorepeat must not trigger a binding");
    static_cast<void>(Manager.process(KEY_F8, 0));
    require(Manager.process(KEY_F9, 1) == Types::HotkeyAction::ToggleRightClicker,
            "F9 must toggle the right clicker");
    static_cast<void>(Manager.process(KEY_F9, 0));
    static_cast<void>(Manager.process(KEY_LEFTCTRL, 1));
    static_cast<void>(Manager.process(KEY_LEFTSHIFT, 1));
    require(!Manager.process(KEY_F8, 1),
            "a channel binding must not fire while unrelated modifiers are held");
    static_cast<void>(Manager.process(KEY_F8, 0));
    require(Manager.process(KEY_F12, 1) == Types::HotkeyAction::ExitAutoclicker,
            "configured modifier chord must request exit");
    Manager.reset();
    require(!Manager.process(KEY_F12, 1), "reset must clear held modifiers");

    // Capture reports the pressed chord instead of dispatching it.
    auto Capturer = Input::HotkeyManager{};
    require(!Capturer.capture(KEY_LEFTCTRL, 1), "a modifier alone must not complete a capture");
    const auto Chord = Capturer.capture(KEY_F7, 1);
    require(Chord.has_value(), "a non-modifier press must complete a capture");
    require(Chord->KeyCode == KEY_F7 && Chord->Control && !Chord->Alt && !Chord->Shift &&
                !Chord->Super,
            "capture must bake in the modifiers held at the time of the press");
    require(!Capturer.capture(KEY_F7, 2), "key autorepeat must not complete a capture");
    require(!Capturer.capture(KEY_F7, 0), "key release must not complete a capture");
    static_cast<void>(Capturer.capture(KEY_LEFTCTRL, 0));

    const auto MouseChord = Capturer.capture(BTN_SIDE, 1);
    require(MouseChord.has_value() && MouseChord->KeyCode == BTN_SIDE,
            "mouse buttons must be capturable as bindings");
    static_cast<void>(Capturer.capture(BTN_SIDE, 0));

    static_cast<void>(Capturer.capture(KEY_LEFTSHIFT, 1));
    Capturer.reset();
    const auto AfterReset = Capturer.capture(KEY_F7, 1);
    require(AfterReset.has_value() && !AfterReset->Shift,
            "reset must clear held modifiers before the next capture");
}

} // namespace Autoclicker::Tests
