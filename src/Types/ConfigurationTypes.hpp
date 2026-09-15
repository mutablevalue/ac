#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace Autoclicker::Types {

// Hold is appended so the persisted and wire encodings of Normal and Additive stay stable.
enum class OperatingMode { Normal, Additive, Hold };
enum class RateUnit { Cps, Milliseconds };

struct KeyChord final {
    std::uint16_t KeyCode{};
    bool Control{};
    bool Alt{};
    bool Shift{};
    bool Super{};

    auto operator==(const KeyChord&) const -> bool = default;
};

struct ChannelConfiguration final {
    OperatingMode Mode{OperatingMode::Normal};
    RateUnit Unit{RateUnit::Cps};
    std::uint16_t Cps{20};
    std::chrono::milliseconds Delay{50};
    std::uint16_t RateOffset{};
    // Additive output stays suppressed until the measured physical rate reaches this value.
    // Zero disables the gate.
    std::uint16_t AdditiveStartCps{};
    // One scheduled click emits this many complete press/release pairs.
    std::uint8_t ClickMultiplier{1};
    // Spacing between the pairs of one burst. Stays well under the desktop double-click window.
    std::chrono::milliseconds MultiplierGap{40};
    // Appends ClickMultiplier - 1 synthetic clicks after each physical tap, even while the
    // channel itself is disabled.
    bool MultiplyPhysicalClicks{};
    KeyChord ToggleBinding{};
};

struct ConfigurationData final {
    ChannelConfiguration Left{.ToggleBinding = {66, false, false, false, false}};
    ChannelConfiguration Right{.ToggleBinding = {67, false, false, false, false}};
    // Stable application identity: a desktop/Flatpak application id where the session provides one,
    // otherwise an X11 window class. Both forms are accepted so targets saved by older builds keep
    // working.
    std::optional<std::string> TargetApplication;
    KeyChord ExitBinding{88, true, false, true, false};
};

} // namespace Autoclicker::Types
