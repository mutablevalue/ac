#pragma once

#include <cstdint>
#include <string>

namespace Autoclicker::Types {

// Where a selectable application was discovered. A Window candidate owns an X11 toplevel and can
// therefore be focus-tracked; a Process candidate is known only from /proc, which is the only
// place a Wayland-native client is visible at all.
enum class ApplicationSource { Window, Process };

// One selectable target. Identity is the stable key persisted in configuration and compared
// against the focused window; DisplayName exists only to be shown to the user.
struct ApplicationCandidate final {
    std::string Identity;
    std::string DisplayName;
    std::string WindowClass;
    std::uint64_t WindowId{};
    std::uint32_t ProcessId{};
    ApplicationSource Source{ApplicationSource::Process};
    // True when an installed desktop entry describes this identity. Background session helpers have
    // none, so this orders them below real applications without ever hiding them.
    bool Installed{};
};

// Raw observation of the focused window. Target matching lives in the daemon because it needs the
// process inventory, which the X11 tracker deliberately knows nothing about.
struct WindowFocusState final {
    bool Supported{};
    bool OwnWindowFocused{};
    // False when the compositor reports focus through an internal proxy window, which is how
    // Mutter represents "a Wayland-native client is focused". Such a client cannot be identified.
    bool FocusObservable{};
    std::uint64_t ActiveWindowId{};
    std::uint32_t ActiveProcessId{};
    std::string ActiveWindowClass;
};

} // namespace Autoclicker::Types
