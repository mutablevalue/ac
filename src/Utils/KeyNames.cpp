#include "Utils/KeyNames.hpp"

#include <algorithm>
#include <array>
#include <linux/input-event-codes.h>

namespace Autoclicker::Utils {
namespace {
using Entry = KeyNames::Entry;

// Ordered by code so lookup can binary search. KeyNameTests asserts the ordering, because a
// hand-maintained table will eventually be edited out of order.
constexpr auto NameTable = std::to_array<Entry>({
    {KEY_ESC, "Escape"},
    {KEY_1, "1"}, {KEY_2, "2"}, {KEY_3, "3"}, {KEY_4, "4"}, {KEY_5, "5"},
    {KEY_6, "6"}, {KEY_7, "7"}, {KEY_8, "8"}, {KEY_9, "9"}, {KEY_0, "0"},
    {KEY_MINUS, "-"}, {KEY_EQUAL, "="}, {KEY_BACKSPACE, "Backspace"}, {KEY_TAB, "Tab"},
    {KEY_Q, "Q"}, {KEY_W, "W"}, {KEY_E, "E"}, {KEY_R, "R"}, {KEY_T, "T"},
    {KEY_Y, "Y"}, {KEY_U, "U"}, {KEY_I, "I"}, {KEY_O, "O"}, {KEY_P, "P"},
    {KEY_LEFTBRACE, "["}, {KEY_RIGHTBRACE, "]"}, {KEY_ENTER, "Enter"},
    {KEY_LEFTCTRL, "Left Ctrl"},
    {KEY_A, "A"}, {KEY_S, "S"}, {KEY_D, "D"}, {KEY_F, "F"}, {KEY_G, "G"},
    {KEY_H, "H"}, {KEY_J, "J"}, {KEY_K, "K"}, {KEY_L, "L"},
    {KEY_SEMICOLON, ";"}, {KEY_APOSTROPHE, "'"}, {KEY_GRAVE, "`"},
    {KEY_LEFTSHIFT, "Left Shift"}, {KEY_BACKSLASH, "\\"},
    {KEY_Z, "Z"}, {KEY_X, "X"}, {KEY_C, "C"}, {KEY_V, "V"}, {KEY_B, "B"},
    {KEY_N, "N"}, {KEY_M, "M"},
    {KEY_COMMA, ","}, {KEY_DOT, "."}, {KEY_SLASH, "/"},
    {KEY_RIGHTSHIFT, "Right Shift"}, {KEY_KPASTERISK, "Numpad *"},
    {KEY_LEFTALT, "Left Alt"}, {KEY_SPACE, "Space"}, {KEY_CAPSLOCK, "Caps Lock"},
    {KEY_F1, "F1"}, {KEY_F2, "F2"}, {KEY_F3, "F3"}, {KEY_F4, "F4"}, {KEY_F5, "F5"},
    {KEY_F6, "F6"}, {KEY_F7, "F7"}, {KEY_F8, "F8"}, {KEY_F9, "F9"}, {KEY_F10, "F10"},
    {KEY_NUMLOCK, "Num Lock"}, {KEY_SCROLLLOCK, "Scroll Lock"},
    {KEY_KP7, "Numpad 7"}, {KEY_KP8, "Numpad 8"}, {KEY_KP9, "Numpad 9"},
    {KEY_KPMINUS, "Numpad -"},
    {KEY_KP4, "Numpad 4"}, {KEY_KP5, "Numpad 5"}, {KEY_KP6, "Numpad 6"},
    {KEY_KPPLUS, "Numpad +"},
    {KEY_KP1, "Numpad 1"}, {KEY_KP2, "Numpad 2"}, {KEY_KP3, "Numpad 3"},
    {KEY_KP0, "Numpad 0"}, {KEY_KPDOT, "Numpad ."},
    {KEY_ZENKAKUHANKAKU, "Zenkaku/Hankaku"}, {KEY_102ND, "102nd"},
    {KEY_F11, "F11"}, {KEY_F12, "F12"},
    {KEY_RO, "Ro"}, {KEY_KATAKANA, "Katakana"}, {KEY_HIRAGANA, "Hiragana"},
    {KEY_HENKAN, "Henkan"}, {KEY_KATAKANAHIRAGANA, "Katakana/Hiragana"},
    {KEY_MUHENKAN, "Muhenkan"}, {KEY_KPJPCOMMA, "Numpad JP ,"},
    {KEY_KPENTER, "Numpad Enter"}, {KEY_RIGHTCTRL, "Right Ctrl"}, {KEY_KPSLASH, "Numpad /"},
    {KEY_SYSRQ, "SysRq"}, {KEY_RIGHTALT, "Right Alt"}, {KEY_LINEFEED, "Linefeed"},
    {KEY_HOME, "Home"}, {KEY_UP, "Up"}, {KEY_PAGEUP, "Page Up"},
    {KEY_LEFT, "Left"}, {KEY_RIGHT, "Right"}, {KEY_END, "End"}, {KEY_DOWN, "Down"},
    {KEY_PAGEDOWN, "Page Down"}, {KEY_INSERT, "Insert"}, {KEY_DELETE, "Delete"},
    {KEY_MACRO, "Macro"}, {KEY_MUTE, "Mute"},
    {KEY_VOLUMEDOWN, "Volume Down"}, {KEY_VOLUMEUP, "Volume Up"}, {KEY_POWER, "Power"},
    {KEY_KPEQUAL, "Numpad ="}, {KEY_KPPLUSMINUS, "Numpad +/-"}, {KEY_PAUSE, "Pause"},
    {KEY_SCALE, "Scale"}, {KEY_KPCOMMA, "Numpad ,"},
    {KEY_HANGEUL, "Hangeul"}, {KEY_HANJA, "Hanja"}, {KEY_YEN, "Yen"},
    {KEY_LEFTMETA, "Left Super"}, {KEY_RIGHTMETA, "Right Super"}, {KEY_COMPOSE, "Compose"},
    {KEY_STOP, "Stop"}, {KEY_AGAIN, "Again"}, {KEY_PROPS, "Props"}, {KEY_UNDO, "Undo"},
    {KEY_FRONT, "Front"}, {KEY_COPY, "Copy"}, {KEY_OPEN, "Open"}, {KEY_PASTE, "Paste"},
    {KEY_FIND, "Find"}, {KEY_CUT, "Cut"}, {KEY_HELP, "Help"}, {KEY_MENU, "Menu"},
    {KEY_CALC, "Calculator"}, {KEY_SETUP, "Setup"}, {KEY_SLEEP, "Sleep"},
    {KEY_WAKEUP, "Wake Up"}, {KEY_FILE, "File"}, {KEY_SENDFILE, "Send File"},
    {KEY_DELETEFILE, "Delete File"}, {KEY_XFER, "Transfer"},
    {KEY_PROG1, "Prog1"}, {KEY_PROG2, "Prog2"}, {KEY_WWW, "WWW"}, {KEY_MSDOS, "MS-DOS"},
    {KEY_SCREENLOCK, "Screen Lock"}, {KEY_ROTATE_DISPLAY, "Rotate Display"},
    {KEY_CYCLEWINDOWS, "Cycle Windows"}, {KEY_MAIL, "Mail"}, {KEY_BOOKMARKS, "Bookmarks"},
    {KEY_COMPUTER, "Computer"}, {KEY_BACK, "Back"}, {KEY_FORWARD, "Forward"},
    {KEY_CLOSECD, "Close CD"}, {KEY_EJECTCD, "Eject CD"},
    {KEY_EJECTCLOSECD, "Eject/Close CD"}, {KEY_NEXTSONG, "Next Song"},
    {KEY_PLAYPAUSE, "Play/Pause"}, {KEY_PREVIOUSSONG, "Previous Song"},
    {KEY_STOPCD, "Stop CD"}, {KEY_RECORD, "Record"}, {KEY_REWIND, "Rewind"},
    {KEY_PHONE, "Phone"}, {KEY_ISO, "ISO"}, {KEY_CONFIG, "Config"},
    {KEY_HOMEPAGE, "Homepage"}, {KEY_REFRESH, "Refresh"}, {KEY_EXIT, "Exit"},
    {KEY_MOVE, "Move"}, {KEY_EDIT, "Edit"},
    {KEY_SCROLLUP, "Scroll Up"}, {KEY_SCROLLDOWN, "Scroll Down"},
    {KEY_KPLEFTPAREN, "Numpad ("}, {KEY_KPRIGHTPAREN, "Numpad )"},
    {KEY_NEW, "New"}, {KEY_REDO, "Redo"},
    {KEY_F13, "F13"}, {KEY_F14, "F14"}, {KEY_F15, "F15"}, {KEY_F16, "F16"},
    {KEY_F17, "F17"}, {KEY_F18, "F18"}, {KEY_F19, "F19"}, {KEY_F20, "F20"},
    {KEY_F21, "F21"}, {KEY_F22, "F22"}, {KEY_F23, "F23"}, {KEY_F24, "F24"},
    {KEY_PLAYCD, "Play CD"}, {KEY_PAUSECD, "Pause CD"},
    {KEY_PROG3, "Prog3"}, {KEY_PROG4, "Prog4"}, {KEY_DASHBOARD, "Dashboard"},
    {KEY_SUSPEND, "Suspend"}, {KEY_CLOSE, "Close"}, {KEY_PLAY, "Play"},
    {KEY_FASTFORWARD, "Fast Forward"}, {KEY_BASSBOOST, "Bass Boost"},
    {KEY_PRINT, "Print"}, {KEY_HP, "HP"}, {KEY_CAMERA, "Camera"}, {KEY_SOUND, "Sound"},
    {KEY_QUESTION, "Question"}, {KEY_EMAIL, "Email"}, {KEY_CHAT, "Chat"},
    {KEY_SEARCH, "Search"}, {KEY_CONNECT, "Connect"}, {KEY_FINANCE, "Finance"},
    {KEY_SPORT, "Sport"}, {KEY_SHOP, "Shop"}, {KEY_ALTERASE, "Alt Erase"},
    {KEY_CANCEL, "Cancel"},
    {KEY_BRIGHTNESSDOWN, "Brightness Down"}, {KEY_BRIGHTNESSUP, "Brightness Up"},
    {KEY_MEDIA, "Media"}, {KEY_SWITCHVIDEOMODE, "Switch Video Mode"},
    {KEY_KBDILLUMTOGGLE, "Keyboard Light"}, {KEY_KBDILLUMDOWN, "Keyboard Light Down"},
    {KEY_KBDILLUMUP, "Keyboard Light Up"},
    {KEY_SEND, "Send"}, {KEY_REPLY, "Reply"}, {KEY_FORWARDMAIL, "Forward Mail"},
    {KEY_SAVE, "Save"}, {KEY_DOCUMENTS, "Documents"}, {KEY_BATTERY, "Battery"},
    {KEY_BLUETOOTH, "Bluetooth"}, {KEY_WLAN, "WLAN"}, {KEY_UWB, "UWB"},
    {BTN_0, "Mouse Button 1"}, {BTN_1, "Mouse Button 2"}, {BTN_2, "Mouse Button 3"},
    {BTN_3, "Mouse Button 4"}, {BTN_4, "Mouse Button 5"}, {BTN_5, "Mouse Button 6"},
    {BTN_6, "Mouse Button 7"}, {BTN_7, "Mouse Button 8"}, {BTN_8, "Mouse Button 9"},
    {BTN_9, "Mouse Button 10"},
    {BTN_LEFT, "Left Mouse Button"}, {BTN_RIGHT, "Right Mouse Button"},
    {BTN_MIDDLE, "Middle Mouse Button"}, {BTN_SIDE, "Mouse Side Button"},
    {BTN_EXTRA, "Mouse Extra Button"}, {BTN_FORWARD, "Mouse Forward Button"},
    {BTN_BACK, "Mouse Back Button"}, {BTN_TASK, "Mouse Task Button"},
});
} // namespace

auto KeyNames::table() noexcept -> std::span<const Entry> { return NameTable; }

auto KeyNames::display_name(const std::uint16_t Code) -> std::string {
    const auto Match = std::ranges::lower_bound(NameTable, Code, {}, &Entry::Code);
    if (Match != NameTable.end() && Match->Code == Code) return std::string{Match->Name};
    return is_mouse_button(Code) ? "Mouse button " + std::to_string(Code - BTN_MISC + 1)
                                 : "Key " + std::to_string(Code);
}

auto KeyNames::chord_name(const Types::KeyChord& Value) -> std::string {
    auto Name = std::string{};
    if (Value.Control) Name += "Ctrl+";
    if (Value.Alt) Name += "Alt+";
    if (Value.Shift) Name += "Shift+";
    if (Value.Super) Name += "Super+";
    return Name + display_name(Value.KeyCode);
}

auto KeyNames::is_mouse_button(const std::uint16_t Code) noexcept -> bool {
    return Code >= BTN_MISC && Code <= BTN_TASK;
}

auto KeyNames::is_modifier(const std::uint16_t Code) noexcept -> bool {
    switch (Code) {
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
    case KEY_LEFTALT:
    case KEY_RIGHTALT:
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT:
    case KEY_LEFTMETA:
    case KEY_RIGHTMETA:
        return true;
    default:
        return false;
    }
}

} // namespace Autoclicker::Utils
