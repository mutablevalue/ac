#pragma once

#include "Ipc/Socket.hpp"
#include "Types/IpcTypes.hpp"
#include "Utils/Error.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <sys/types.h>

namespace Autoclicker::Gui {

class DaemonClient final {
public:
    DaemonClient() = default;
    ~DaemonClient();
    DaemonClient(const DaemonClient&) = delete;
    auto operator=(const DaemonClient&) -> DaemonClient& = delete;

    auto start(const std::filesystem::path& SocketPath) -> std::expected<void, Utils::Error>;
    auto shutdown() -> void;
    auto set_configuration(const Types::ConfigurationData& Value) -> std::expected<void, Utils::Error>;
    auto set_own_window(std::uint64_t WindowId) -> std::expected<void, Utils::Error>;
    auto set_enabled(bool Enabled) -> std::expected<void, Utils::Error>;
    auto request_status() -> std::expected<void, Utils::Error>;
    auto poll() -> void;

    [[nodiscard]] auto connected() const noexcept -> bool;
    [[nodiscard]] auto exit_requested() const noexcept -> bool;
    [[nodiscard]] auto cached_status() const -> const std::optional<Types::DaemonStatus>&;
    [[nodiscard]] auto status_revision() const noexcept -> std::uint64_t;
    [[nodiscard]] auto take_error() -> std::optional<std::string>;

private:
    auto transact(Types::IpcMessage Request) -> std::expected<Types::IpcMessage, Utils::Error>;
    auto send(Types::IpcMessage Message) -> std::expected<void, Utils::Error>;
    auto consume_async(const Types::IpcMessage& Message) -> bool;
    auto wait_for_exit(std::chrono::milliseconds Timeout) -> bool;

    Ipc::Socket Connection;
    Utils::UniqueFd InstanceLock;
    std::filesystem::path SocketPath;
    pid_t ProcessId{-1};
    bool ExitRequested{};
    bool OwnsDaemon{};
    std::optional<Types::DaemonStatus> CachedStatus;
    std::uint64_t StatusRevision{};
    std::optional<std::string> PendingError;
};

} // namespace Autoclicker::Gui
