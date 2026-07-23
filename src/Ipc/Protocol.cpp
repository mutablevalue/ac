#include "Ipc/Protocol.hpp"

#include <nlohmann/json.hpp>

namespace Autoclicker::Ipc {
namespace {
using Json = nlohmann::json;
using namespace Types;

auto command_name(const IpcCommand Value) -> std::string_view {
    switch (Value) {
    case IpcCommand::Hello: return "hello";
    case IpcCommand::SetConfiguration: return "set_configuration";
    case IpcCommand::SetOwnWindow: return "set_own_window";
    case IpcCommand::SetEnabled: return "set_enabled";
    case IpcCommand::GetStatus: return "get_status";
    case IpcCommand::Shutdown: return "shutdown";
    case IpcCommand::ExitRequested: return "exit_requested";
    case IpcCommand::Ack: return "ack";
    case IpcCommand::Error: return "error";
    case IpcCommand::Status: return "status";
    }
    return "error";
}

auto parse_command(const std::string& Value) -> IpcCommand {
    if (Value == "hello") return IpcCommand::Hello;
    if (Value == "set_configuration") return IpcCommand::SetConfiguration;
    if (Value == "set_own_window") return IpcCommand::SetOwnWindow;
    if (Value == "set_enabled") return IpcCommand::SetEnabled;
    if (Value == "get_status") return IpcCommand::GetStatus;
    if (Value == "shutdown") return IpcCommand::Shutdown;
    if (Value == "exit_requested") return IpcCommand::ExitRequested;
    if (Value == "ack") return IpcCommand::Ack;
    if (Value == "status") return IpcCommand::Status;
    return IpcCommand::Error;
}

auto chord_json(const KeyChord& Value) -> Json {
    return {{"key", Value.KeyCode}, {"ctrl", Value.Control}, {"alt", Value.Alt},
            {"shift", Value.Shift}, {"super", Value.Super}};
}

auto parse_chord(const Json& Value) -> KeyChord {
    return {.KeyCode = Value.at("key").get<std::uint16_t>(),
            .Control = Value.value("ctrl", false), .Alt = Value.value("alt", false),
            .Shift = Value.value("shift", false), .Super = Value.value("super", false)};
}

auto config_json(const ConfigurationData& Value) -> Json {
    auto Result = Json{{"mode", static_cast<int>(Value.Mode)}, {"rate_unit", static_cast<int>(Value.Unit)},
            {"cps", Value.Cps}, {"delay_ms", Value.Delay.count()}, {"rate_offset", Value.RateOffset},
            {"toggle", chord_json(Value.ToggleBinding)},
            {"exit", chord_json(Value.ExitBinding)}};
    Result["target_application"] = Value.TargetApplication ? Json(*Value.TargetApplication) : Json(nullptr);
    return Result;
}

auto parse_config(const Json& Value) -> ConfigurationData {
    auto Result = ConfigurationData{};
    Result.Mode = static_cast<OperatingMode>(Value.at("mode").get<int>());
    Result.Unit = static_cast<RateUnit>(Value.at("rate_unit").get<int>());
    Result.Cps = Value.at("cps").get<std::uint16_t>();
    Result.Delay = std::chrono::milliseconds{Value.at("delay_ms").get<std::int64_t>()};
    Result.RateOffset = Value.value("rate_offset", static_cast<std::uint16_t>(0));
    if (Value.contains("target_application") && !Value.at("target_application").is_null()) {
        Result.TargetApplication = Value.at("target_application").get<std::string>();
    }
    Result.ToggleBinding = parse_chord(Value.at("toggle"));
    Result.ExitBinding = parse_chord(Value.at("exit"));
    return Result;
}

auto status_json(const DaemonStatus& Value) -> Json {
    return {{"lifecycle", static_cast<int>(Value.Lifecycle)}, {"mode", static_cast<int>(Value.Mode)},
            {"mouse_connected", Value.MouseConnected}, {"physical_cps", Value.PhysicalCps},
            {"focus_tracking_supported", Value.FocusTrackingSupported},
            {"focus_allowed", Value.FocusAllowed}, {"emitted_cps", Value.EmittedCps},
            {"active_application", Value.ActiveApplication}, {"latest_error", Value.LatestError}};
}

auto parse_status(const Json& Value) -> DaemonStatus {
    return {.Lifecycle = static_cast<LifecycleState>(Value.at("lifecycle").get<int>()),
            .Mode = static_cast<OperatingMode>(Value.at("mode").get<int>()),
            .MouseConnected = Value.value("mouse_connected", false),
            .FocusTrackingSupported = Value.value("focus_tracking_supported", false),
            .FocusAllowed = Value.value("focus_allowed", false),
            .PhysicalCps = Value.value("physical_cps", 0.0),
            .EmittedCps = Value.value("emitted_cps", 0.0),
            .ActiveApplication = Value.value("active_application", std::string{}),
            .LatestError = Value.value("latest_error", std::string{})};
}
} // namespace

auto Protocol::encode(const Types::IpcMessage& Message) -> std::string {
    auto Value = Json{{"command", command_name(Message.Command)}, {"version", Message.Version},
                      {"message", Message.Message}};
    if (Message.Configuration) Value["configuration"] = config_json(*Message.Configuration);
    if (Message.Enabled) Value["enabled"] = *Message.Enabled;
    if (Message.WindowId) Value["window_id"] = *Message.WindowId;
    if (Message.Status) Value["status"] = status_json(*Message.Status);
    return Value.dump();
}

auto Protocol::decode(const std::string_view Payload) -> std::expected<Types::IpcMessage, Utils::Error> {
    try {
        const auto Value = Json::parse(Payload);
        auto Result = Types::IpcMessage{};
        Result.Command = parse_command(Value.at("command").get<std::string>());
        Result.Version = Value.value("version", 0U);
        Result.Message = Value.value("message", std::string{});
        if (Value.contains("configuration")) Result.Configuration = parse_config(Value.at("configuration"));
        if (Value.contains("enabled")) Result.Enabled = Value.at("enabled").get<bool>();
        if (Value.contains("window_id")) Result.WindowId = Value.at("window_id").get<std::uint64_t>();
        if (Value.contains("status")) Result.Status = parse_status(Value.at("status"));
        return Result;
    } catch (const std::exception& Exception) {
        return std::unexpected{Utils::Error{Types::ErrorCode::Protocol, Exception.what()}};
    }
}

} // namespace Autoclicker::Ipc
