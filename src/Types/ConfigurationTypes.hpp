#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace Autoclicker::Types {

enum class OperatingMode { Normal, Additive };
enum class RateUnit { Cps, Milliseconds };

struct KeyChord final {
    std::uint16_t KeyCode{};
    bool Control{};
    bool Alt{};
    bool Shift{};
    bool Super{};

    auto operator==(const KeyChord&) const -> bool = default;
};

struct ConfigurationData final {
    OperatingMode Mode{OperatingMode::Normal};
    RateUnit Unit{RateUnit::Cps};
    std::uint16_t Cps{20};
    std::chrono::milliseconds Delay{50};
    std::uint16_t RateOffset{};
    std::optional<std::string> TargetApplication;
    KeyChord ToggleBinding{66, false, false, false, false};
    KeyChord ExitBinding{88, true, false, true, false};
};

} // namespace Autoclicker::Types
