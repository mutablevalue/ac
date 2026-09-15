#include "Input/ApplicationInventory.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <sstream>
#include <utility>

namespace Autoclicker::Input {
namespace {

// systemd launcher prefixes sit between "app-" and the application identity. Stripping the prefix
// generically avoids caring which launcher started the process.
constexpr auto LauncherPrefixes = std::array<std::string_view, 4>{"flatpak-", "gnome-", "glib-", "snap-"};

auto read_file(const std::filesystem::path& Path) -> std::optional<std::string> {
    auto Stream = std::ifstream{Path, std::ios::binary};
    if (!Stream) return std::nullopt;
    auto Buffer = std::ostringstream{};
    Buffer << Stream.rdbuf();
    return std::move(Buffer).str();
}

auto parse_process_id(const std::string_view Name) -> std::optional<std::uint32_t> {
    auto Value = std::uint32_t{};
    const auto* const End = Name.data() + Name.size();
    const auto Result = std::from_chars(Name.data(), End, Value);
    if (Result.ec != std::errc{} || Result.ptr != End) return std::nullopt;
    return Value;
}

// The Flatpak sandbox reports its own identity even when the host cgroup is unhelpful.
auto flatpak_identity(const std::uint32_t ProcessId) -> std::optional<std::string> {
    const auto Contents = read_file(std::filesystem::path{"/proc"} / std::to_string(ProcessId) / "root/.flatpak-info");
    if (!Contents) return std::nullopt;
    auto InApplication = false;
    for (const auto Line : std::views::split(std::string_view{*Contents}, '\n')) {
        const auto Text = std::string_view{Line.begin(), Line.end()};
        if (Text.starts_with('[')) {
            InApplication = Text.starts_with("[Application]");
            continue;
        }
        if (InApplication && Text.starts_with("name=")) {
            auto Name = std::string{Text.substr(5)};
            if (!Name.empty() && Name.back() == '\r') Name.pop_back();
            if (!Name.empty()) return Name;
        }
    }
    return std::nullopt;
}

auto desktop_search_directories() -> std::vector<std::filesystem::path> {
    auto Result = std::vector<std::filesystem::path>{};
    const auto* const DataHome = std::getenv("XDG_DATA_HOME");
    const auto* const Home = std::getenv("HOME");
    if (DataHome != nullptr && *DataHome != '\0') {
        Result.emplace_back(std::filesystem::path{DataHome} / "applications");
    } else if (Home != nullptr && *Home != '\0') {
        Result.emplace_back(std::filesystem::path{Home} / ".local/share/applications");
    }
    const auto* const DataDirs = std::getenv("XDG_DATA_DIRS");
    const auto Directories = std::string_view{DataDirs != nullptr && *DataDirs != '\0'
                                                  ? DataDirs
                                                  : "/usr/local/share:/usr/share"};
    for (const auto Entry : std::views::split(Directories, ':')) {
        const auto Text = std::string_view{Entry.begin(), Entry.end()};
        if (!Text.empty()) Result.emplace_back(std::filesystem::path{Text} / "applications");
    }
    return Result;
}

} // namespace

auto ApplicationInventory::sort_candidates(std::vector<Types::ApplicationCandidate>& Candidates) -> void {
    // Installed applications first, then alphabetically. Session helpers keep their place in the
    // list rather than being guessed away, which no name-based filter could do safely.
    std::ranges::sort(Candidates, [](const Types::ApplicationCandidate& Left,
                                     const Types::ApplicationCandidate& Right) {
        if (Left.Installed != Right.Installed) return Left.Installed;
        return Left.DisplayName < Right.DisplayName;
    });
}

auto ApplicationInventory::unescape_unit_name(const std::string_view Value) -> std::string {
    auto Result = std::string{};
    Result.reserve(Value.size());
    for (auto Index = std::size_t{}; Index < Value.size(); ++Index) {
        if (Value[Index] == '\\' && Index + 3 < Value.size() && Value[Index + 1] == 'x') {
            auto Decoded = 0U;
            const auto* const Begin = Value.data() + Index + 2;
            if (std::from_chars(Begin, Begin + 2, Decoded, 16).ec == std::errc{}) {
                Result.push_back(static_cast<char>(Decoded));
                Index += 3;
                continue;
            }
        }
        Result.push_back(Value[Index]);
    }
    return Result;
}

auto ApplicationInventory::identity_from_cgroup(const std::string_view Cgroup) -> std::optional<std::string> {
    // A cgroup file may carry several lines; the unified hierarchy line is the one that matters.
    for (const auto Line : std::views::split(Cgroup, '\n')) {
        auto Text = std::string_view{Line.begin(), Line.end()};
        if (!Text.empty() && Text.back() == '\r') Text.remove_suffix(1);
        const auto Slash = Text.rfind('/');
        if (Slash == std::string_view::npos) continue;
        auto Unit = Text.substr(Slash + 1);
        if (!Unit.starts_with("app-") || !Unit.ends_with(".scope")) continue;
        Unit.remove_prefix(4);
        Unit.remove_suffix(6);
        // Trailing "-<random>" is systemd's uniqueness suffix, not part of the identity.
        if (const auto Dash = Unit.rfind('-'); Dash != std::string_view::npos) {
            const auto Suffix = Unit.substr(Dash + 1);
            if (!Suffix.empty() && std::ranges::all_of(Suffix, [](const char Character) {
                    return Character >= '0' && Character <= '9';
                })) {
                Unit = Unit.substr(0, Dash);
            }
        }
        for (const auto Prefix : LauncherPrefixes) {
            if (Unit.starts_with(Prefix)) {
                Unit.remove_prefix(Prefix.size());
                break;
            }
        }
        if (Unit.empty()) continue;
        auto Identity = unescape_unit_name(Unit);
        if (!Identity.empty()) return Identity;
    }
    return std::nullopt;
}

auto ApplicationInventory::desktop_entry_name(const std::string_view Contents) -> std::string {
    auto InEntry = false;
    for (const auto Line : std::views::split(Contents, '\n')) {
        auto Text = std::string_view{Line.begin(), Line.end()};
        if (!Text.empty() && Text.back() == '\r') Text.remove_suffix(1);
        if (Text.starts_with('[')) {
            if (InEntry) break;
            InEntry = Text.starts_with("[Desktop Entry]");
            continue;
        }
        // Name[xx]= is a localised variant; the unlocalised key is the one we want.
        if (InEntry && Text.starts_with("Name=")) return std::string{Text.substr(5)};
    }
    return {};
}

auto ApplicationInventory::resolve_desktop_entry(const std::string& Identity) -> std::string {
    if (const auto Cached = DisplayNames.find(Identity); Cached != DisplayNames.end()) {
        return Cached->second;
    }
    auto Name = std::string{};
    auto Error = std::error_code{};
    for (const auto& Directory : desktop_search_directories()) {
        const auto Candidate = Directory / (Identity + ".desktop");
        if (!std::filesystem::exists(Candidate, Error)) continue;
        if (const auto Contents = read_file(Candidate)) {
            Name = desktop_entry_name(*Contents);
            if (!Name.empty()) break;
        }
    }
    // Cache the identity itself for a miss so repeated lookups stop touching the filesystem, but
    // report the miss so the caller can tell an installed application from a session helper.
    DisplayNames.emplace(Identity, Name.empty() ? Identity : Name);
    return Name;
}

auto ApplicationInventory::refresh() -> void {
    Applications.clear();
    IdentityByProcess.clear();
    Alive.clear();

    // Lowest process id per identity wins so a candidate points at the launcher rather than at
    // whichever helper happened to be scanned first.
    auto Representative = std::unordered_map<std::string, std::uint32_t>{};
    auto Error = std::error_code{};
    for (const auto& Entry : std::filesystem::directory_iterator{"/proc", Error}) {
        const auto ProcessId = parse_process_id(Entry.path().filename().string());
        if (!ProcessId) continue;
        const auto Cgroup = read_file(Entry.path() / "cgroup");
        if (!Cgroup) continue;
        auto Identity = identity_from_cgroup(*Cgroup);
        if (!Identity) Identity = flatpak_identity(*ProcessId);
        if (!Identity) continue;
        IdentityByProcess.emplace(*ProcessId, *Identity);
        Alive.insert(*Identity);
        const auto [Slot, Inserted] = Representative.try_emplace(*Identity, *ProcessId);
        if (!Inserted) Slot->second = std::min(Slot->second, *ProcessId);
    }

    Applications.reserve(Representative.size());
    for (auto& [Identity, ProcessId] : Representative) {
        auto Name = resolve_desktop_entry(Identity);
        const auto Installed = !Name.empty();
        Applications.push_back({.Identity = Identity,
                                .DisplayName = Installed ? std::move(Name) : Identity,
                                .WindowClass = {},
                                .WindowId = 0,
                                .ProcessId = ProcessId,
                                .Source = Types::ApplicationSource::Process,
                                .Installed = Installed});
    }
    sort_candidates(Applications);
}

auto ApplicationInventory::refresh_if_stale(const std::chrono::steady_clock::time_point Now,
                                            const std::chrono::milliseconds MaximumAge) -> void {
    if (LastRefresh && Now - *LastRefresh < MaximumAge) return;
    LastRefresh = Now;
    refresh();
}

auto ApplicationInventory::identify(const std::uint32_t ProcessId) const -> std::string {
    if (ProcessId == 0) return {};
    if (const auto Known = IdentityByProcess.find(ProcessId); Known != IdentityByProcess.end()) {
        return Known->second;
    }
    const auto Cgroup = read_file(std::filesystem::path{"/proc"} / std::to_string(ProcessId) / "cgroup");
    if (!Cgroup) return {};
    if (auto Identity = identity_from_cgroup(*Cgroup)) return *std::move(Identity);
    return flatpak_identity(ProcessId).value_or(std::string{});
}

auto ApplicationInventory::display_name(const std::string_view Identity) const -> std::string {
    const auto Known = DisplayNames.find(std::string{Identity});
    return Known != DisplayNames.end() ? Known->second : std::string{Identity};
}

auto ApplicationInventory::running(const std::string_view Identity) const -> bool {
    return Alive.contains(std::string{Identity});
}

} // namespace Autoclicker::Input
