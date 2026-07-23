#pragma once

#include "Core/ClickScheduler.hpp"
#include "Core/ShutdownCoordinator.hpp"
#include "Input/HotkeyManager.hpp"
#include "Input/InputListener.hpp"
#include "Input/VirtualMouse.hpp"
#include "Input/WindowFocusTracker.hpp"
#include "Ipc/Socket.hpp"
#include "Utils/UniqueFd.hpp"

#include <expected>
#include <filesystem>
#include <string>

namespace Autoclicker::Core {

class Daemon final {
public:
    explicit Daemon(std::filesystem::path SocketPath);
    ~Daemon();

    auto run() -> std::expected<void, Utils::Error>;

private:
    auto initialize() -> std::expected<void, Utils::Error>;
    auto handle_message(const Types::IpcMessage& Message) -> void;
    auto handle_input(const Types::RawInputEvent& Event) -> void;
    auto send_status() -> void;
    auto arm_timer() -> void;
    auto request_exit() -> void;
    auto refresh_focus(ClickScheduler::TimePoint Now) -> bool;
    [[nodiscard]] auto status() const -> Types::DaemonStatus;

    std::filesystem::path SocketPath;
    Input::VirtualMouse VirtualMouse;
    ClickScheduler Scheduler;
    ShutdownCoordinator Shutdown;
    Input::InputListener InputListener;
    Input::HotkeyManager Hotkeys;
    Input::WindowFocusTracker FocusTracker;
    Ipc::Socket Server;
    Ipc::Socket Client;
    Utils::UniqueFd Epoll;
    Utils::UniqueFd Timer;
    Utils::UniqueFd Signals;
    Utils::UniqueFd InstanceLock;
    Types::ConfigurationData Config;
    Types::WindowFocusState FocusState;
    std::uint64_t OwnWindowId{};
    std::string LatestError;
};

} // namespace Autoclicker::Core
