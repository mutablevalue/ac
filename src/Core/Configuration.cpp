#include "Core/Configuration.hpp"

#include "Utils/KeyNames.hpp"

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <linux/input-event-codes.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace Autoclicker::Core {
namespace {
using Json = nlohmann::json;
using Types::ChannelConfiguration;
using Types::ConfigurationData;
using Types::KeyChord;
using Types::OperatingMode;
using Types::RateUnit;

auto chord_json(const KeyChord& Value) -> Json {
    return {{"key_code", Value.KeyCode}, {"control", Value.Control}, {"alt", Value.Alt},
            {"shift", Value.Shift}, {"super", Value.Super}};
}

auto mode_name(const OperatingMode Value) -> const char* {
    switch (Value) {
    case OperatingMode::Additive: return "additive";
    case OperatingMode::Hold: return "hold";
    default: return "normal";
    }
}

auto channel_json(const ChannelConfiguration& Value) -> Json {
    return {
        {"mode", mode_name(Value.Mode)},
        {"rate_unit", Value.Unit == RateUnit::Cps ? "cps" : "milliseconds"},
        {"cps", Value.Cps},
        {"delay_ms", Value.Delay.count()},
        {"rate_offset", Value.RateOffset},
        {"additive_start_cps", Value.AdditiveStartCps},
        {"click_multiplier", Value.ClickMultiplier},
        {"multiplier_gap_ms", Value.MultiplierGap.count()},
        {"multiply_physical_clicks", Value.MultiplyPhysicalClicks},
        {"toggle_binding", chord_json(Value.ToggleBinding)},
    };
}

auto to_json_value(const ConfigurationData& Value) -> Json {
    auto Result = Json{
        {"left", channel_json(Value.Left)},
        {"right", channel_json(Value.Right)},
        {"exit_binding", chord_json(Value.ExitBinding)},
    };
    Result["target_application"] = Value.TargetApplication ? Json(*Value.TargetApplication) : Json(nullptr);
    return Result;
}

auto parse_chord(const Json& Value) -> KeyChord {
    return {
        .KeyCode = Value.at("key_code").get<std::uint16_t>(),
        .Control = Value.value("control", false),
        .Alt = Value.value("alt", false),
        .Shift = Value.value("shift", false),
        .Super = Value.value("super", false),
    };
}

// Also reads the pre-channel flat layout, where these keys sat at the document root.
auto parse_channel(const Json& Value, ChannelConfiguration Result) -> ChannelConfiguration {
    const auto ModeName = Value.value("mode", "normal");
    Result.Mode = ModeName == "hold" ? OperatingMode::Hold
        : ModeName == "additive" || ModeName == "mirrored" ? OperatingMode::Additive
                                                           : OperatingMode::Normal;
    const auto UnitName = Value.value("rate_unit", Value.value("normal_rate_unit", "cps"));
    Result.Unit = UnitName == "milliseconds" || UnitName == "interval"
        ? RateUnit::Milliseconds : RateUnit::Cps;
    Result.Cps = Value.value("cps", Result.Cps);
    Result.Delay = std::chrono::milliseconds{
        Value.value("delay_ms", Value.value("interval_ms", Result.Delay.count()))};
    Result.RateOffset = Value.value("rate_offset", Result.RateOffset);
    Result.AdditiveStartCps = Value.value("additive_start_cps", Result.AdditiveStartCps);
    // Absent in configurations written before the multiplier existed; the defaults are inert.
    Result.ClickMultiplier = Value.value("click_multiplier", Result.ClickMultiplier);
    Result.MultiplierGap = std::chrono::milliseconds{
        Value.value("multiplier_gap_ms", Result.MultiplierGap.count())};
    Result.MultiplyPhysicalClicks =
        Value.value("multiply_physical_clicks", Result.MultiplyPhysicalClicks);
    if (Value.contains("toggle_binding")) Result.ToggleBinding = parse_chord(Value.at("toggle_binding"));
    return Result;
}

auto from_json_value(const Json& Value) -> ConfigurationData {
    auto Result = ConfigurationData{};
    if (Value.contains("left")) {
        Result.Left = parse_channel(Value.at("left"), Result.Left);
        if (Value.contains("right")) Result.Right = parse_channel(Value.at("right"), Result.Right);
    } else {
        // Configurations written before per-button channels described a single left clicker.
        Result.Left = parse_channel(Value, Result.Left);
        if (Result.Right.ToggleBinding == Result.Left.ToggleBinding) {
            Result.Right.ToggleBinding = {KEY_F10, false, false, false, false};
        }
    }
    if (Value.contains("target_application") && !Value.at("target_application").is_null()) {
        Result.TargetApplication = Value.at("target_application").get<std::string>();
    }
    if (Value.contains("exit_binding")) Result.ExitBinding = parse_chord(Value.at("exit_binding"));
    return Result;
}

auto bindable_key(const KeyChord& Value) -> bool {
    // Mouse buttons live in the same EV_KEY code space and are deliberately bindable.
    return Value.KeyCode != 0 && Value.KeyCode <= KEY_MAX &&
           !Utils::KeyNames::is_modifier(Value.KeyCode);
}

auto validate_channel(const ChannelConfiguration& Value, const std::string_view Name,
                      const std::uint16_t OwnButtonCode) -> std::expected<void, Utils::Error> {
    const auto Reject = [Name](const std::string& Reason) {
        return std::unexpected{Utils::Error{Types::ErrorCode::InvalidConfiguration,
                                            std::string{Name} + " channel: " + Reason}};
    };
    if ((Value.Mode != OperatingMode::Normal && Value.Mode != OperatingMode::Additive &&
         Value.Mode != OperatingMode::Hold) ||
        (Value.Unit != RateUnit::Cps && Value.Unit != RateUnit::Milliseconds)) {
        return Reject("configuration contains an unknown mode");
    }
    if (Value.Cps < 1 || Value.Cps > 1000) return Reject("CPS must be between 1 and 1000");
    if (Value.Delay < std::chrono::milliseconds{1} || Value.Delay > std::chrono::milliseconds{10000}) {
        return Reject("delay must be between 1 and 10000 ms");
    }
    if (Value.RateOffset > 10000) return Reject("rate offset must be between 0 and 10000");
    if (Value.AdditiveStartCps > 1000) return Reject("additive start rate must be between 0 and 1000");
    if (Value.ClickMultiplier < 1 || Value.ClickMultiplier > 10) {
        return Reject("click multiplier must be between 1 and 10");
    }
    // The ceiling keeps a doubled click inside the desktop double-click window.
    if (Value.MultiplierGap < std::chrono::milliseconds{5} ||
        Value.MultiplierGap > std::chrono::milliseconds{150}) {
        return Reject("multiplier gap must be between 5 and 150 ms");
    }
    // Hold captures its own button, so binding the toggle to it would disarm the channel
    // the moment the user tried to drive it.
    if (Value.Mode == OperatingMode::Hold && Value.ToggleBinding.KeyCode == OwnButtonCode) {
        return Reject("hold mode cannot be toggled by the button it captures");
    }
    return {};
}
} // namespace

auto Configuration::instance() -> Configuration& {
    static Configuration Instance;
    return Instance;
}

auto Configuration::snapshot() const -> Types::ConfigurationData {
    const auto Lock = std::shared_lock{Mutex};
    return Data;
}

auto Configuration::update(Types::ConfigurationData Value) -> std::expected<void, Utils::Error> {
    if (auto Result = validate(Value); !Result) return std::unexpected{Result.error()};
    const auto Lock = std::unique_lock{Mutex};
    Data = std::move(Value);
    return {};
}

auto Configuration::load(const std::filesystem::path& Path) -> std::expected<void, Utils::Error> {
    if (!std::filesystem::exists(Path)) return {};
    try {
        auto Stream = std::ifstream{Path};
        if (!Stream) {
            return std::unexpected{Utils::Error{Types::ErrorCode::Io, "Unable to open configuration", std::error_code{errno, std::generic_category()}}};
        }
        const auto Parsed = from_json_value(Json::parse(Stream));
        return update(Parsed);
    } catch (const std::exception& Exception) {
        return std::unexpected{Utils::Error{Types::ErrorCode::InvalidConfiguration, Exception.what()}};
    }
}

auto Configuration::save(const std::filesystem::path& Path) const -> std::expected<void, Utils::Error> {
    try {
        std::filesystem::create_directories(Path.parent_path());
        const auto Temporary = Path.string() + ".tmp." + std::to_string(getpid());
        {
            auto Stream = std::ofstream{Temporary, std::ios::trunc};
            if (!Stream) {
                return std::unexpected{Utils::Error{Types::ErrorCode::Io, "Unable to create temporary configuration", std::error_code{errno, std::generic_category()}}};
            }
            Stream << to_json_value(snapshot()).dump(2) << '\n';
            Stream.flush();
            if (!Stream) {
                return std::unexpected{Utils::Error{Types::ErrorCode::Io, "Unable to write configuration"}};
            }
        }
        std::filesystem::rename(Temporary, Path);
        return {};
    } catch (const std::exception& Exception) {
        return std::unexpected{Utils::Error{Types::ErrorCode::Io, Exception.what()}};
    }
}

auto Configuration::validate(const Types::ConfigurationData& Value) -> std::expected<void, Utils::Error> {
    if (auto Result = validate_channel(Value.Left, "Left", BTN_LEFT); !Result) return Result;
    if (auto Result = validate_channel(Value.Right, "Right", BTN_RIGHT); !Result) return Result;
    if (Value.TargetApplication &&
        (Value.TargetApplication->empty() || Value.TargetApplication->size() > 256 ||
         std::ranges::any_of(*Value.TargetApplication, [](const char Character) {
             return static_cast<unsigned char>(Character) < 0x20U;
         }))) {
        return std::unexpected{Utils::Error{Types::ErrorCode::InvalidConfiguration,
            "Target application must be a printable application identity no longer than 256 characters"}};
    }
    if (!bindable_key(Value.Left.ToggleBinding) || !bindable_key(Value.Right.ToggleBinding) ||
        !bindable_key(Value.ExitBinding)) {
        return std::unexpected{Utils::Error{Types::ErrorCode::InvalidConfiguration, "Bindings require a non-modifier key"}};
    }
    if (Value.Left.ToggleBinding == Value.Right.ToggleBinding ||
        Value.Left.ToggleBinding == Value.ExitBinding ||
        Value.Right.ToggleBinding == Value.ExitBinding) {
        return std::unexpected{Utils::Error{Types::ErrorCode::InvalidConfiguration, "Every binding must differ"}};
    }
    return {};
}

} // namespace Autoclicker::Core
