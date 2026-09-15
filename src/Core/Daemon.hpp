#pragma once

#include "Core/ClickScheduler.hpp"
#include "Core/ShutdownCoordinator.hpp"
#include "Input/ApplicationInventory.hpp"
#include "Input/HotkeyManager.hpp"
#include "Input/InputListener.hpp"
#include "Input/VirtualMouse.hpp"
#include "Input/WindowFocusTracker.hpp"
#include "Ipc/Socket.hpp"
#include "Utils/UniqueFd.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

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
    [[nodiscard]] auto target_focused(ClickScheduler::TimePoint Now) -> bool;
    [[nodiscard]] auto matches_target(std::string_view Identity, std::string_view WindowClass) const -> bool;
    auto toggle_channel(Types::MouseButton Button) -> void;
    auto begin_capture(std::uint32_t Token, ClickScheduler::TimePoint Now) -> void;
    auto finish_capture(Types::CaptureOutcome Outcome, Types::KeyChord Chord) -> void;
    [[nodiscard]] auto capture_active() const noexcept -> bool;
    [[nodiscard]] auto grab_required() const -> bool;
    auto update_grab() -> void;
    [[nodiscard]] auto channel_status(const ClickScheduler& Scheduler,
                                      ClickScheduler::TimePoint Now) const -> Types::ChannelStatus;
    [[nodiscard]] auto status() const -> Types::DaemonStatus;

    std::filesystem::path SocketPath;
    Input::VirtualMouse VirtualMouse;
    ClickScheduler LeftScheduler;
    ClickScheduler RightScheduler;
    ShutdownCoordinator Shutdown;
    Input::InputListener InputListener;
    Input::HotkeyManager Hotkeys;
    Input::WindowFocusTracker FocusTracker;
    Input::ApplicationInventory Inventory;
    Ipc::Socket Server;
    Ipc::Socket Client;
    Utils::UniqueFd Epoll;
    Utils::UniqueFd Timer;
    Utils::UniqueFd Signals;
    Utils::UniqueFd InstanceLock;
    Types::ConfigurationData Config;
    Types::WindowFocusState FocusState;
    // Resolved from FocusState each refresh so status reporting never repeats the lookup.
    std::string ActiveApplication;
    bool FocusAllowed{};
    // Set while focus is unobservable and a target is configured; drives a slow liveness poll so a
    // target that starts or exits is noticed without any X11 event to react to.
    std::optional<ClickScheduler::TimePoint> FocusPollDeadline;
    std::optional<ClickScheduler::TimePoint> CaptureDeadline;
    std::optional<ClickScheduler::TimePoint> CaptureArmedAt;
    std::uint32_t CaptureToken{};
    std::uint64_t OwnWindowId{};
    std::string LatestError;
};

} // namespace Autoclicker::Core
