#include "TestHarness.hpp"

#include "Ipc/Protocol.hpp"

#include <linux/input-event-codes.h>

namespace Autoclicker::Tests {

auto protocol_tests() -> void {
    auto Configuration = Types::ConfigurationData{};
    Configuration.Left.RateOffset = 2;
    Configuration.Left.AdditiveStartCps = 5;
    Configuration.Left.ClickMultiplier = 4;
    Configuration.Left.MultiplierGap = std::chrono::milliseconds{55};
    Configuration.Left.MultiplyPhysicalClicks = true;
    Configuration.Right.Mode = Types::OperatingMode::Additive;
    Configuration.Right.Cps = 11;
    Configuration.TargetApplication = "Minecraft";
    auto Message = Types::IpcMessage{.Command = Types::IpcCommand::SetConfiguration,
                                     .Configuration = Configuration};
    const auto Encoded = Ipc::Protocol::encode(Message);
    const auto Decoded = Ipc::Protocol::decode(Encoded);
    require(Decoded.has_value(), "encoded protocol message must decode");
    require(Decoded->Version == Types::IpcMessage::ProtocolVersion, "protocol version must round trip");
    require(Decoded->Command == Types::IpcCommand::SetConfiguration, "command must round trip");
    require(Decoded->Configuration && Decoded->Configuration->Left.Cps == 20,
            "configuration must round trip");
    require(Decoded->Configuration && Decoded->Configuration->Left.RateOffset == 2,
            "rate offset must round trip");
    require(Decoded->Configuration && Decoded->Configuration->Left.AdditiveStartCps == 5,
            "additive start rate must round trip");
    require(Decoded->Configuration && Decoded->Configuration->Right.Cps == 11 &&
                Decoded->Configuration->Right.Mode == Types::OperatingMode::Additive,
            "the right channel must round trip");
    require(Decoded->Configuration && Decoded->Configuration->Left.ToggleBinding !=
                Decoded->Configuration->Right.ToggleBinding,
            "per-channel toggle bindings must round trip independently");
    require(Decoded->Configuration && Decoded->Configuration->TargetApplication == "Minecraft",
            "target application must round trip");
    require(Decoded->Configuration && Decoded->Configuration->Left.ClickMultiplier == 4 &&
                Decoded->Configuration->Left.MultiplierGap == std::chrono::milliseconds{55} &&
                Decoded->Configuration->Left.MultiplyPhysicalClicks,
            "the multiplier settings must round trip");

    auto HoldConfiguration = Types::ConfigurationData{};
    HoldConfiguration.Left.Mode = Types::OperatingMode::Hold;
    const auto DecodedHold = Ipc::Protocol::decode(Ipc::Protocol::encode(
        {.Command = Types::IpcCommand::SetConfiguration, .Configuration = HoldConfiguration}));
    require(DecodedHold && DecodedHold->Configuration &&
                DecodedHold->Configuration->Left.Mode == Types::OperatingMode::Hold,
            "hold mode must round trip over the wire");

    const auto EnableMessage = Types::IpcMessage{.Command = Types::IpcCommand::SetEnabled,
                                                 .Enabled = true,
                                                 .Button = Types::MouseButton::Right};
    const auto DecodedEnable = Ipc::Protocol::decode(Ipc::Protocol::encode(EnableMessage));
    require(DecodedEnable && DecodedEnable->Enabled == true &&
                DecodedEnable->Button == Types::MouseButton::Right,
            "the targeted button must round trip with an enable request");

    auto Status = Types::DaemonStatus{};
    Status.Lifecycle = Types::LifecycleState::Enabled;
    Status.Left = {.Enabled = true, .Mode = Types::OperatingMode::Additive,
                   .PhysicalCps = 9.0, .EmittedCps = 6.0, .AdditiveGateOpen = false};
    Status.Right = {.Enabled = false, .Mode = Types::OperatingMode::Normal,
                    .PhysicalCps = 0.0, .EmittedCps = 4.0, .AdditiveGateOpen = true};
    const auto StatusMessage = Types::IpcMessage{.Command = Types::IpcCommand::Status,
                                                 .Status = Status};
    const auto DecodedStatus = Ipc::Protocol::decode(Ipc::Protocol::encode(StatusMessage));
    require(DecodedStatus && DecodedStatus->Status, "status must round trip");
    require(DecodedStatus->Status->Left.Enabled && DecodedStatus->Status->Left.EmittedCps == 6.0 &&
                !DecodedStatus->Status->Left.AdditiveGateOpen,
            "the left channel status must round trip");
    require(!DecodedStatus->Status->Right.Enabled && DecodedStatus->Status->Right.EmittedCps == 4.0,
            "the right channel status must round trip");

    // Focus that is supported but unobservable is the Wayland case, and the GUI explains it
    // differently from focus that is missing outright, so the two flags must stay independent.
    auto Degraded = Types::DaemonStatus{};
    Degraded.FocusTrackingSupported = true;
    Degraded.FocusObservable = false;
    Degraded.FocusAllowed = true;
    Degraded.ActiveApplication = "";
    const auto DecodedDegraded = Ipc::Protocol::decode(Ipc::Protocol::encode(
        {.Command = Types::IpcCommand::Status, .Status = Degraded}));
    require(DecodedDegraded && DecodedDegraded->Status &&
                DecodedDegraded->Status->FocusTrackingSupported &&
                !DecodedDegraded->Status->FocusObservable && DecodedDegraded->Status->FocusAllowed,
            "unobservable focus must round trip without collapsing into unsupported focus");

    const auto WindowMessage = Types::IpcMessage{.Command = Types::IpcCommand::SetOwnWindow,
                                                  .WindowId = 42};
    const auto DecodedWindow = Ipc::Protocol::decode(Ipc::Protocol::encode(WindowMessage));
    require(DecodedWindow && DecodedWindow->WindowId == 42, "GUI window identity must round trip");

    require(Types::IpcMessage::ProtocolVersion == 5,
            "reporting focus observability separately from focus support requires protocol 5");
    const auto DecodedBegin = Ipc::Protocol::decode(Ipc::Protocol::encode(
        {.Command = Types::IpcCommand::BeginBindingCapture,
         .Capture = Types::BindingCapture{.Token = 7}}));
    require(DecodedBegin && DecodedBegin->Command == Types::IpcCommand::BeginBindingCapture &&
                DecodedBegin->Capture && DecodedBegin->Capture->Token == 7,
            "a capture request must round trip with its token");

    const auto DecodedCaptured = Ipc::Protocol::decode(Ipc::Protocol::encode(
        {.Command = Types::IpcCommand::BindingCaptured,
         .Capture = Types::BindingCapture{.Token = 9,
                                          .Outcome = Types::CaptureOutcome::Captured,
                                          .Chord = {KEY_F7, true, false, true, false}}}));
    require(DecodedCaptured && DecodedCaptured->Command == Types::IpcCommand::BindingCaptured &&
                DecodedCaptured->Capture && DecodedCaptured->Capture->Token == 9 &&
                DecodedCaptured->Capture->Outcome == Types::CaptureOutcome::Captured &&
                DecodedCaptured->Capture->Chord == Types::KeyChord{KEY_F7, true, false, true, false},
            "a captured binding must round trip its token, outcome and chord");

    auto CaptureStatus = Types::DaemonStatus{};
    CaptureStatus.CaptureActive = true;
    const auto DecodedCaptureStatus = Ipc::Protocol::decode(Ipc::Protocol::encode(
        {.Command = Types::IpcCommand::Status, .Status = CaptureStatus}));
    require(DecodedCaptureStatus && DecodedCaptureStatus->Status &&
                DecodedCaptureStatus->Status->CaptureActive,
            "an armed capture must be visible in the status");

    require(!Ipc::Protocol::decode("not-json"), "malformed protocol payload must fail");
}

} // namespace Autoclicker::Tests
