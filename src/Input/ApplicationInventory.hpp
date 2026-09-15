#pragma once

#include "Types/WindowTypes.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Autoclicker::Input {

// Enumerates running desktop applications from /proc.
//
// This is the only source that can see a Wayland-native client. Such a client owns no X11 toplevel,
// so _NET_CLIENT_LIST cannot find it, and no compositor here exposes a foreign-toplevel protocol.
// Identity comes from the desktop's own accounting rather than any built-in list of applications:
// every application the session launches lives in a systemd `app-*.scope` cgroup naming it.
class ApplicationInventory final {
public:
    // "app-flatpak-com.discordapp.Discord-3402706409.scope" -> "com.discordapp.Discord"
    // "app-gnome-org.mozilla.firefox-6742.scope"            -> "org.mozilla.firefox"
    // Returns nothing for a process the session did not launch as an application, which is what
    // keeps helper daemons and shell children out of the picker.
    [[nodiscard]] static auto identity_from_cgroup(std::string_view Cgroup) -> std::optional<std::string>;

    // systemd escapes anything outside [A-Za-z0-9:-_.] as \xNN inside a unit name.
    [[nodiscard]] static auto unescape_unit_name(std::string_view Value) -> std::string;

    // Reads Name= from the [Desktop Entry] group. Later groups are actions, not the application.
    [[nodiscard]] static auto desktop_entry_name(std::string_view Contents) -> std::string;

    // Installed applications first, then alphabetical. Shared with the GUI so a merged list keeps
    // the same ordering as an unmerged one.
    static auto sort_candidates(std::vector<Types::ApplicationCandidate>& Candidates) -> void;

    // Rescans /proc. Cheap enough to call about once a second; never call it per click.
    auto refresh() -> void;
    auto refresh_if_stale(std::chrono::steady_clock::time_point Now,
                          std::chrono::milliseconds MaximumAge = std::chrono::milliseconds{1000}) -> void;

    [[nodiscard]] auto applications() const -> const std::vector<Types::ApplicationCandidate>& {
        return Applications;
    }
    // Identity of a process, resolved from the cached scan and falling back to a direct read so a
    // freshly focused window is never misidentified as unknown.
    [[nodiscard]] auto identify(std::uint32_t ProcessId) const -> std::string;
    // Display name for an identity, or the identity itself when no desktop entry describes it.
    [[nodiscard]] auto display_name(std::string_view Identity) const -> std::string;
    [[nodiscard]] auto running(std::string_view Identity) const -> bool;

private:
    // Returns the desktop entry's Name=, or empty when no entry describes the identity.
    [[nodiscard]] auto resolve_desktop_entry(const std::string& Identity) -> std::string;

    std::vector<Types::ApplicationCandidate> Applications;
    std::unordered_map<std::uint32_t, std::string> IdentityByProcess;
    std::unordered_map<std::string, std::string> DisplayNames;
    std::unordered_set<std::string> Alive;
    std::optional<std::chrono::steady_clock::time_point> LastRefresh;
};

} // namespace Autoclicker::Input
