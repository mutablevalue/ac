#include "TestHarness.hpp"

#include "Input/ApplicationInventory.hpp"

namespace Autoclicker::Tests {
namespace {
using Input::ApplicationInventory;

auto identity(const std::string_view Cgroup) -> std::string {
    return ApplicationInventory::identity_from_cgroup(Cgroup).value_or(std::string{});
}

auto scope_identity_tests() -> void {
    require(identity("0::/user.slice/user-1000.slice/user@1000.service/app.slice/"
                     "app-flatpak-com.discordapp.Discord-3402706409.scope") == "com.discordapp.Discord",
            "A Flatpak scope names the application it launched");
    require(identity("0::/user.slice/user-1000.slice/user@1000.service/app.slice/"
                     "app-gnome-org.mozilla.firefox-6742.scope") == "org.mozilla.firefox",
            "A gnome-launched scope drops the launcher prefix");
    require(identity("0::/user.slice/user-1000.slice/user@1000.service/app.slice/"
                     "app-gnome-kitty-7387.scope") == "kitty",
            "A plain binary keeps its own name as its identity");
    require(identity("0::/user.slice/user-1000.slice/user@1000.service/app.slice/"
                     "app-flatpak-org.vinegarhq.Sober-1234.scope") == "org.vinegarhq.Sober",
            "Sober is identified like any other Flatpak application");
}

auto non_application_tests() -> void {
    require(!ApplicationInventory::identity_from_cgroup(
                "0::/user.slice/user-1000.slice/user@1000.service/app.slice/kitty-30045-0.scope"),
            "A scope that is not an app-* scope is not an application");
    require(!ApplicationInventory::identity_from_cgroup(
                "0::/system.slice/NetworkManager.service"),
            "A system service is not a selectable application");
    require(!ApplicationInventory::identity_from_cgroup(""), "Empty cgroup data yields no identity");
    require(!ApplicationInventory::identity_from_cgroup("0::/"), "A bare root cgroup yields no identity");
}

auto escaping_tests() -> void {
    require(identity("0::/user.slice/app.slice/app-gnome-polychromatic\\x2dautostart-3830.scope") ==
                "polychromatic-autostart",
            "systemd \\xNN escapes are decoded back into the identity");
    require(ApplicationInventory::unescape_unit_name("plain") == "plain",
            "An unescaped name survives unchanged");
    require(ApplicationInventory::unescape_unit_name("trailing\\x2") == "trailing\\x2",
            "A truncated escape is left alone rather than dropped");
}

auto identity_suffix_tests() -> void {
    // Only a fully numeric suffix is systemd's uniqueness marker; a version is part of the name.
    require(identity("0::/app.slice/app-gnome-libreoffice-writer-99.scope") == "libreoffice-writer",
            "The numeric uniqueness suffix is stripped");
    require(identity("0::/app.slice/app-gnome-gimp-2.10-4242.scope") == "gimp-2.10",
            "A version in the application name is preserved");
}

auto desktop_entry_tests() -> void {
    constexpr auto Entry = "[Desktop Entry]\n"
                           "Type=Application\n"
                           "Name=Discord\n"
                           "Exec=discord\n"
                           "[Desktop Action new]\n"
                           "Name=New Window\n";
    require(ApplicationInventory::desktop_entry_name(Entry) == "Discord",
            "The display name comes from the entry group, not from an action");
    require(ApplicationInventory::desktop_entry_name("[Desktop Entry]\nName[de]=Zug\nName=Train\n") == "Train",
            "A localised name never displaces the unlocalised one");
    require(ApplicationInventory::desktop_entry_name("[Desktop Entry]\nExec=thing\n").empty(),
            "An entry without a name reports none");
    require(ApplicationInventory::desktop_entry_name("").empty(), "Empty contents report no name");
}

auto liveness_tests() -> void {
    // Scanning the real /proc must never fault, and this process is always in some cgroup.
    auto Inventory = ApplicationInventory{};
    Inventory.refresh();
    require(!Inventory.running("com.example.NotRunning"), "An absent application is not reported running");
    require(Inventory.display_name("com.example.NotInstalled") == "com.example.NotInstalled",
            "An unknown identity falls back to itself as a display name");
    require(Inventory.identify(0).empty(), "Process id zero identifies nothing");
    for (const auto& Candidate : Inventory.applications()) {
        require(!Candidate.Identity.empty(), "Every candidate carries an identity");
        require(!Candidate.DisplayName.empty(), "Every candidate carries a display name");
    }
}
} // namespace

auto application_inventory_tests() -> void {
    scope_identity_tests();
    non_application_tests();
    escaping_tests();
    identity_suffix_tests();
    desktop_entry_tests();
    liveness_tests();
}

} // namespace Autoclicker::Tests
