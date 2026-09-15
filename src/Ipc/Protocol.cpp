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
    case IpcCommand::BeginBindingCapture: return "begin_binding_capture";
    case IpcCommand::CancelBindingCapture: return "cancel_binding_capture";
    case IpcCommand::BindingCaptured: return "binding_captured";
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
    if (Value == "begin_binding_capture") return IpcCommand::BeginBindingCapture;
    if (Value == "cancel_binding_capture") return IpcCommand::CancelBindingCapture;
    if (Value == "binding_captured") return IpcCommand::BindingCaptured;
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

auto capture_json(const BindingCapture& Value) -> Json {
    return {{"token", Value.Token}, {"outcome", static_cast<int>(Value.Outcome)},
            {"chord", chord_json(Value.Chord)}};
}

auto parse_capture(const Json& Value) -> BindingCapture {
    auto Result = BindingCapture{};
    Result.Token = Value.value("token", 0U);
    Result.Outcome = static_cast<CaptureOutcome>(Value.value("outcome", 0));
    if (Value.contains("chord")) Result.Chord = parse_chord(Value.at("chord"));
    return Result;
}

auto channel_json(const ChannelConfiguration& Value) -> Json {
    return {{"mode", static_cast<int>(Value.Mode)}, {"rate_unit", static_cast<int>(Value.Unit)},
            {"cps", Value.Cps}, {"delay_ms", Value.Delay.count()}, {"rate_offset", Value.RateOffset},
            {"additive_start_cps", Value.AdditiveStartCps},
            {"click_multiplier", Value.ClickMultiplier},
            {"multiplier_gap_ms", Value.MultiplierGap.count()},
            {"multiply_physical", Value.MultiplyPhysicalClicks},
            {"toggle", chord_json(Value.ToggleBinding)}};
}

auto parse_channel(const Json& Value) -> ChannelConfiguration {
    auto Result = ChannelConfiguration{};
    Result.Mode = static_cast<OperatingMode>(Value.at("mode").get<int>());
    Result.Unit = static_cast<RateUnit>(Value.at("rate_unit").get<int>());
    Result.Cps = Value.at("cps").get<std::uint16_t>();
    Result.Delay = std::chrono::milliseconds{Value.at("delay_ms").get<std::int64_t>()};
    Result.RateOffset = Value.value("rate_offset", static_cast<std::uint16_t>(0));
    Result.AdditiveStartCps = Value.value("additive_start_cps", static_cast<std::uint16_t>(0));
    Result.ClickMultiplier = Value.value("click_multiplier", static_cast<std::uint8_t>(1));
    Result.MultiplierGap = std::chrono::milliseconds{
        Value.value("multiplier_gap_ms", std::int64_t{40})};
    Result.MultiplyPhysicalClicks = Value.value("multiply_physical", false);
    Result.ToggleBinding = parse_chord(Value.at("toggle"));
    return Result;
}

auto config_json(const ConfigurationData& Value) -> Json {
    auto Result = Json{{"left", channel_json(Value.Left)}, {"right", channel_json(Value.Right)},
                       {"exit", chord_json(Value.ExitBinding)}};
    Result["target_application"] = Value.TargetApplication ? Json(*Value.TargetApplication) : Json(nullptr);
    return Result;
}

auto parse_config(const Json& Value) -> ConfigurationData {
    auto Result = ConfigurationData{};
    Result.Left = parse_channel(Value.at("left"));
    Result.Right = parse_channel(Value.at("right"));
    if (Value.contains("target_application") && !Value.at("target_application").is_null()) {
        Result.TargetApplication = Value.at("target_application").get<std::string>();
    }
    Result.ExitBinding = parse_chord(Value.at("exit"));
    return Result;
}

auto channel_status_json(const ChannelStatus& Value) -> Json {
    return {{"enabled", Value.Enabled}, {"mode", static_cast<int>(Value.Mode)},
            {"physical_cps", Value.PhysicalCps}, {"emitted_cps", Value.EmittedCps},
            {"additive_gate_open", Value.AdditiveGateOpen}};
}

auto parse_channel_status(const Json& Value) -> ChannelStatus {
    return {.Enabled = Value.value("enabled", false),
            .Mode = static_cast<OperatingMode>(Value.value("mode", 0)),
            .PhysicalCps = Value.value("physical_cps", 0.0),
            .EmittedCps = Value.value("emitted_cps", 0.0),
            .AdditiveGateOpen = Value.value("additive_gate_open", true)};
}

auto status_json(const DaemonStatus& Value) -> Json {
    return {{"lifecycle", static_cast<int>(Value.Lifecycle)},
            {"left", channel_status_json(Value.Left)}, {"right", channel_status_json(Value.Right)},
            {"mouse_connected", Value.MouseConnected},
            {"focus_tracking_supported", Value.FocusTrackingSupported},
            {"focus_observable", Value.FocusObservable},
            {"focus_allowed", Value.FocusAllowed},
            {"active_application", Value.ActiveApplication}, {"latest_error", Value.LatestError},
            {"capture_active", Value.CaptureActive}};
}

auto parse_status(const Json& Value) -> DaemonStatus {
    return {.Lifecycle = static_cast<LifecycleState>(Value.at("lifecycle").get<int>()),
            .Left = parse_channel_status(Value.at("left")),
            .Right = parse_channel_status(Value.at("right")),
            .MouseConnected = Value.value("mouse_connected", false),
            .FocusTrackingSupported = Value.value("focus_tracking_supported", false),
            .FocusObservable = Value.value("focus_observable", false),
            .FocusAllowed = Value.value("focus_allowed", false),
            .ActiveApplication = Value.value("active_application", std::string{}),
            .LatestError = Value.value("latest_error", std::string{}),
            .CaptureActive = Value.value("capture_active", false)};
}
} // namespace

auto Protocol::encode(const Types::IpcMessage& Message) -> std::string {
    auto Value = Json{{"command", command_name(Message.Command)}, {"version", Message.Version},
                      {"message", Message.Message}};
    if (Message.Configuration) Value["configuration"] = config_json(*Message.Configuration);
    if (Message.Enabled) Value["enabled"] = *Message.Enabled;
    if (Message.Button) Value["button"] = static_cast<int>(*Message.Button);
    if (Message.WindowId) Value["window_id"] = *Message.WindowId;
    if (Message.Status) Value["status"] = status_json(*Message.Status);
    if (Message.Capture) Value["capture"] = capture_json(*Message.Capture);
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
        if (Value.contains("button")) Result.Button = static_cast<MouseButton>(Value.at("button").get<int>());
        if (Value.contains("window_id")) Result.WindowId = Value.at("window_id").get<std::uint64_t>();
        if (Value.contains("status")) Result.Status = parse_status(Value.at("status"));
        if (Value.contains("capture")) Result.Capture = parse_capture(Value.at("capture"));
        return Result;
    } catch (const std::exception& Exception) {
        return std::unexpected{Utils::Error{Types::ErrorCode::Protocol, Exception.what()}};
    }
}

} // namespace Autoclicker::Ipc
