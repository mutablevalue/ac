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
};

enum class LifecycleState { Disabled, Enabled, ShuttingDown, Stopped };

struct DaemonStatus final {
    LifecycleState Lifecycle{LifecycleState::Disabled};
    OperatingMode Mode{OperatingMode::Normal};
    bool MouseConnected{};
    bool FocusTrackingSupported{};
    bool FocusAllowed{};
    double PhysicalCps{};
    double EmittedCps{};
    std::string ActiveApplication;
    std::string LatestError;
};

struct IpcMessage final {
    static constexpr std::uint32_t ProtocolVersion = 2;

    IpcCommand Command{IpcCommand::Hello};
    std::uint32_t Version{ProtocolVersion};
    std::optional<ConfigurationData> Configuration;
    std::optional<bool> Enabled;
    std::optional<std::uint64_t> WindowId;
    std::optional<DaemonStatus> Status;
    std::string Message;
};

} // namespace Autoclicker::Types
