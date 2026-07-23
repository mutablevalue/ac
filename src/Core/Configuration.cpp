#include "Core/Configuration.hpp"

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <linux/input-event-codes.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <system_error>
#include <unistd.h>

namespace Autoclicker::Core {
namespace {
using Json = nlohmann::json;
using Types::ConfigurationData;
using Types::KeyChord;
using Types::OperatingMode;
using Types::RateUnit;

auto to_json_value(const ConfigurationData& Value) -> Json {
    auto Result = Json{
        {"mode", Value.Mode == OperatingMode::Normal ? "normal" : "additive"},
        {"rate_unit", Value.Unit == RateUnit::Cps ? "cps" : "milliseconds"},
        {"cps", Value.Cps},
        {"delay_ms", Value.Delay.count()},
        {"rate_offset", Value.RateOffset},
        {"toggle_binding", {
            {"key_code", Value.ToggleBinding.KeyCode}, {"control", Value.ToggleBinding.Control},
            {"alt", Value.ToggleBinding.Alt}, {"shift", Value.ToggleBinding.Shift},
            {"super", Value.ToggleBinding.Super}}},
        {"exit_binding", {
            {"key_code", Value.ExitBinding.KeyCode}, {"control", Value.ExitBinding.Control},
            {"alt", Value.ExitBinding.Alt}, {"shift", Value.ExitBinding.Shift},
            {"super", Value.ExitBinding.Super}}},
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

auto from_json_value(const Json& Value) -> ConfigurationData {
    auto Result = ConfigurationData{};
    const auto ModeName = Value.value("mode", "normal");
    Result.Mode = ModeName == "additive" || ModeName == "mirrored"
        ? OperatingMode::Additive : OperatingMode::Normal;
    const auto UnitName = Value.value("rate_unit", Value.value("normal_rate_unit", "cps"));
    Result.Unit = UnitName == "milliseconds" || UnitName == "interval"
        ? RateUnit::Milliseconds : RateUnit::Cps;
    Result.Cps = Value.value("cps", Result.Cps);
    Result.Delay = std::chrono::milliseconds{
        Value.value("delay_ms", Value.value("interval_ms", Result.Delay.count()))};
    Result.RateOffset = Value.value("rate_offset", Result.RateOffset);
    if (Value.contains("target_application") && !Value.at("target_application").is_null()) {
        Result.TargetApplication = Value.at("target_application").get<std::string>();
    }
    if (Value.contains("toggle_binding")) Result.ToggleBinding = parse_chord(Value.at("toggle_binding"));
    if (Value.contains("exit_binding")) Result.ExitBinding = parse_chord(Value.at("exit_binding"));
    return Result;
}

auto modifier_key(const std::uint16_t Code) -> bool {
    return Code == KEY_LEFTCTRL || Code == KEY_RIGHTCTRL || Code == KEY_LEFTALT ||
           Code == KEY_RIGHTALT || Code == KEY_LEFTSHIFT || Code == KEY_RIGHTSHIFT ||
           Code == KEY_LEFTMETA || Code == KEY_RIGHTMETA;
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
    if ((Value.Mode != Types::OperatingMode::Normal && Value.Mode != Types::OperatingMode::Additive) ||
        (Value.Unit != Types::RateUnit::Cps && Value.Unit != Types::RateUnit::Milliseconds)) {
        return std::unexpected{Utils::Error{Types::ErrorCode::InvalidConfiguration, "Configuration contains an unknown mode"}};
    }
    if (Value.Cps < 1 || Value.Cps > 1000) {
        return std::unexpected{Utils::Error{Types::ErrorCode::InvalidConfiguration, "CPS must be between 1 and 1000"}};
    }
    if (Value.Delay < std::chrono::milliseconds{1} || Value.Delay > std::chrono::milliseconds{10000}) {
        return std::unexpected{Utils::Error{Types::ErrorCode::InvalidConfiguration, "Delay must be between 1 and 10000 ms"}};
    }
    if (Value.RateOffset > 10000) {
        return std::unexpected{Utils::Error{Types::ErrorCode::InvalidConfiguration, "Rate offset must be between 0 and 10000"}};
    }
    if (Value.TargetApplication &&
        (Value.TargetApplication->empty() || Value.TargetApplication->size() > 256 ||
         std::ranges::any_of(*Value.TargetApplication, [](const char Character) {
             return static_cast<unsigned char>(Character) < 0x20U;
         }))) {
        return std::unexpected{Utils::Error{Types::ErrorCode::InvalidConfiguration,
            "Target application must be a printable window class no longer than 256 characters"}};
    }
    if (Value.ToggleBinding.KeyCode == 0 || Value.ExitBinding.KeyCode == 0 ||
        Value.ToggleBinding.KeyCode > KEY_MAX || Value.ExitBinding.KeyCode > KEY_MAX ||
        modifier_key(Value.ToggleBinding.KeyCode) || modifier_key(Value.ExitBinding.KeyCode)) {
        return std::unexpected{Utils::Error{Types::ErrorCode::InvalidConfiguration, "Bindings require a non-modifier key"}};
    }
    if (Value.ToggleBinding == Value.ExitBinding) {
        return std::unexpected{Utils::Error{Types::ErrorCode::InvalidConfiguration, "Toggle and exit bindings must differ"}};
    }
    return {};
}

auto key_chord_name(const Types::KeyChord& Value) -> std::string {
    auto Result = std::string{};
    if (Value.Control) Result += "Ctrl+";
    if (Value.Alt) Result += "Alt+";
    if (Value.Shift) Result += "Shift+";
    if (Value.Super) Result += "Super+";
    Result += "Key " + std::to_string(Value.KeyCode);
    return Result;
}

} // namespace Autoclicker::Core
