#pragma once

#include "Types/ConfigurationTypes.hpp"
#include "Types/InputTypes.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace Autoclicker::Types {

enum class IpcCommand {
    Hello,
    SetConfiguration,
    SetOwnWindow,
    SetEnabled,
    GetStatus,
    Shutdown,
    ExitRequested,
    Ack,
    Error,
    Status,
    BeginBindingCapture,
    CancelBindingCapture,
    BindingCaptured,
};

enum class LifecycleState { Disabled, Enabled, ShuttingDown, Stopped };

struct ChannelStatus final {
    bool Enabled{};
    OperatingMode Mode{OperatingMode::Normal};
    double PhysicalCps{};
    double EmittedCps{};
    bool AdditiveGateOpen{true};
};

// Carries a capture request in one direction and its result in the other. The opaque token
// lets the client discard a result belonging to a capture it has already abandoned.
struct BindingCapture final {
    std::uint32_t Token{};
    CaptureOutcome Outcome{CaptureOutcome::Cancelled};
    KeyChord Chord{};
};

struct DaemonStatus final {
    LifecycleState Lifecycle{LifecycleState::Disabled};
    ChannelStatus Left;
    ChannelStatus Right;
    bool MouseConnected{};
    bool FocusTrackingSupported{};
    // False while a Wayland-native client holds focus, which no unprivileged client can identify.
    // The target is then gated on being running rather than on being focused.
    bool FocusObservable{};
    bool FocusAllowed{};
    std::string ActiveApplication;
    std::string LatestError;
    bool CaptureActive{};
};

struct IpcMessage final {
    static constexpr std::uint32_t ProtocolVersion = 5;

    IpcCommand Command{IpcCommand::Hello};
    std::uint32_t Version{ProtocolVersion};
    std::optional<ConfigurationData> Configuration;
    std::optional<bool> Enabled;
    std::optional<MouseButton> Button;
    std::optional<std::uint64_t> WindowId;
    std::optional<DaemonStatus> Status;
    std::string Message;
    std::optional<BindingCapture> Capture;
};

} // namespace Autoclicker::Types
