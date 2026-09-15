#include "Gui/DaemonClient.hpp"

#include "Utils/Paths.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <thread>
#include <utility>
#include <unistd.h>
#include <sys/wait.h>

extern char** environ;

namespace Autoclicker::Gui {
namespace {
auto process_error(const std::string& Message, const int Code = errno) -> Utils::Error {
    return {Types::ErrorCode::Process, Message, std::error_code{Code, std::generic_category()}};
}

auto daemon_path() -> std::filesystem::path {
    auto Buffer = std::array<char, 4096>{};
    const auto Count = readlink("/proc/self/exe", Buffer.data(), Buffer.size() - 1);
    if (Count > 0) return std::filesystem::path{std::string_view{Buffer.data(), static_cast<std::size_t>(Count)}}.parent_path() / "autoclickerd";
    return "autoclickerd";
}
} // namespace

DaemonClient::~DaemonClient() { shutdown(); }

auto DaemonClient::start(const std::filesystem::path& Path) -> std::expected<void, Utils::Error> {
    SocketPath = Path;
    std::error_code DirectoryError;
    std::filesystem::create_directories(SocketPath.parent_path(), DirectoryError);
    if (DirectoryError) return std::unexpected{Utils::Error{Types::ErrorCode::Io, "Unable to create runtime directory", DirectoryError}};
    static_cast<void>(chmod(SocketPath.parent_path().c_str(), 0700));
    const auto LockPath = SocketPath.string() + ".gui.lock";
    InstanceLock.reset(open(LockPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600));
    if (!InstanceLock || flock(InstanceLock.get(), LOCK_EX | LOCK_NB) < 0) {
        return std::unexpected{process_error("Another FastClicker GUI is already running", EBUSY)};
    }
    const auto Executable = daemon_path().string();
    const auto SocketText = SocketPath.string();
    auto Arguments = std::array<char*, 4>{const_cast<char*>(Executable.c_str()),
        const_cast<char*>("--socket"), const_cast<char*>(SocketText.c_str()), nullptr};
    const auto SpawnResult = posix_spawn(&ProcessId, Executable.c_str(), nullptr, nullptr, Arguments.data(), environ);
    if (SpawnResult != 0) return std::unexpected{process_error("Unable to launch autoclicker backend", SpawnResult)};
    OwnsDaemon = true;

    for (auto Attempt = 0; Attempt < 100; ++Attempt) {
        if (auto Result = Ipc::Socket::connect(SocketPath); Result) {
            Connection = std::move(*Result);
            auto Hello = transact({.Command = Types::IpcCommand::Hello});
            if (!Hello || Hello->Command != Types::IpcCommand::Ack) {
                return std::unexpected{Hello ? Utils::Error{Types::ErrorCode::Protocol, "Daemon rejected handshake"} : Hello.error()};
            }
            return {};
        }
        auto Status = 0;
        if (waitpid(ProcessId, &Status, WNOHANG) == ProcessId) {
            ProcessId = -1;
            return std::unexpected{process_error("Autoclicker backend exited during startup", 0)};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return std::unexpected{process_error("Timed out connecting to autoclicker backend", ETIMEDOUT)};
}

auto DaemonClient::consume_async(const Types::IpcMessage& Message) -> bool {
    if (Message.Command == Types::IpcCommand::Status && Message.Status) {
        CachedStatus = Message.Status;
        ++StatusRevision;
        return true;
    }
    if (Message.Command == Types::IpcCommand::ExitRequested) {
        ExitRequested = true;
        return true;
    }
    if (Message.Command == Types::IpcCommand::BindingCaptured && Message.Capture) {
        PendingCapture = Message.Capture;
        return true;
    }
    if (Message.Command == Types::IpcCommand::Error) {
        PendingError = Message.Message;
        return true;
    }
    if (Message.Command == Types::IpcCommand::Ack) return true;
    return false;
}

auto DaemonClient::transact(Types::IpcMessage Request) -> std::expected<Types::IpcMessage, Utils::Error> {
    if (!Connection) return std::unexpected{Utils::Error{Types::ErrorCode::Protocol, "Daemon is not connected"}};
    if (auto Result = Connection.send(Request); !Result) return std::unexpected{Result.error()};
    for (;;) {
        auto Descriptor = pollfd{.fd = Connection.descriptor(), .events = POLLIN, .revents = 0};
        if (::poll(&Descriptor, 1, 1000) <= 0) {
            return std::unexpected{Utils::Error{Types::ErrorCode::Protocol, "Backend handshake timed out"}};
        }
        auto Response = Connection.receive();
        if (!Response) return std::unexpected{Response.error()};
        // A captured binding is an unsolicited push, never a response to this request.
        if (Response->Command != Types::IpcCommand::Status &&
            Response->Command != Types::IpcCommand::ExitRequested &&
            Response->Command != Types::IpcCommand::BindingCaptured) {
            return Response;
        }
        static_cast<void>(consume_async(*Response));
    }
}

auto DaemonClient::send(Types::IpcMessage Message) -> std::expected<void, Utils::Error> {
    if (!Connection) {
        return std::unexpected{Utils::Error{Types::ErrorCode::Protocol, "Backend is not connected"}};
    }
    return Connection.send(Message);
}

auto DaemonClient::set_configuration(const Types::ConfigurationData& Value) -> std::expected<void, Utils::Error> {
    return send({.Command = Types::IpcCommand::SetConfiguration, .Configuration = Value});
}

auto DaemonClient::set_own_window(const std::uint64_t WindowId) -> std::expected<void, Utils::Error> {
    return send({.Command = Types::IpcCommand::SetOwnWindow, .WindowId = WindowId});
}

auto DaemonClient::set_enabled(const Types::MouseButton Button, const bool Enabled)
    -> std::expected<void, Utils::Error> {
    return send({.Command = Types::IpcCommand::SetEnabled, .Enabled = Enabled, .Button = Button});
}

auto DaemonClient::request_status() -> std::expected<void, Utils::Error> {
    return send({.Command = Types::IpcCommand::GetStatus});
}

auto DaemonClient::begin_binding_capture(const std::uint32_t Token) -> std::expected<void, Utils::Error> {
    PendingCapture.reset();
    return send({.Command = Types::IpcCommand::BeginBindingCapture,
                 .Capture = Types::BindingCapture{.Token = Token}});
}

auto DaemonClient::cancel_binding_capture() -> std::expected<void, Utils::Error> {
    return send({.Command = Types::IpcCommand::CancelBindingCapture});
}

auto DaemonClient::take_capture() -> std::optional<Types::BindingCapture> {
    return std::exchange(PendingCapture, std::nullopt);
}

auto DaemonClient::poll() -> void {
    if (!Connection) return;
    auto Descriptor = pollfd{.fd = Connection.descriptor(), .events = POLLIN, .revents = 0};
    while (::poll(&Descriptor, 1, 0) > 0) {
        if ((Descriptor.revents & POLLIN) != 0) {
            auto Message = Connection.receive();
            if (!Message) { ExitRequested = true; break; }
            static_cast<void>(consume_async(*Message));
        }
        if ((Descriptor.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
            ExitRequested = true;
            break;
        }
        Descriptor.revents = 0;
    }
}

auto DaemonClient::wait_for_exit(const std::chrono::milliseconds Timeout) -> bool {
    if (ProcessId <= 0) return true;
    const auto Deadline = std::chrono::steady_clock::now() + Timeout;
    auto Status = 0;
    do {
        if (waitpid(ProcessId, &Status, WNOHANG) == ProcessId) { ProcessId = -1; return true; }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    } while (std::chrono::steady_clock::now() < Deadline);
    return false;
}

auto DaemonClient::shutdown() -> void {
    if (Connection) {
        static_cast<void>(Connection.send({.Command = Types::IpcCommand::Shutdown}));
    }
    if (!wait_for_exit(std::chrono::milliseconds{750}) && ProcessId > 0) {
        static_cast<void>(kill(ProcessId, SIGTERM));
        if (!wait_for_exit(std::chrono::milliseconds{500}) && ProcessId > 0) {
            static_cast<void>(kill(ProcessId, SIGKILL));
            auto Status = 0;
            while (waitpid(ProcessId, &Status, 0) < 0 && errno == EINTR) {}
            ProcessId = -1;
        }
    }
    Connection = Ipc::Socket{};
    if (OwnsDaemon) {
        std::error_code ErrorValue;
        std::filesystem::remove(SocketPath, ErrorValue);
    }
    OwnsDaemon = false;
    InstanceLock.reset();
}

auto DaemonClient::connected() const noexcept -> bool { return static_cast<bool>(Connection); }
auto DaemonClient::exit_requested() const noexcept -> bool { return ExitRequested; }
auto DaemonClient::cached_status() const -> const std::optional<Types::DaemonStatus>& { return CachedStatus; }
auto DaemonClient::status_revision() const noexcept -> std::uint64_t { return StatusRevision; }
auto DaemonClient::take_error() -> std::optional<std::string> { return std::exchange(PendingError, std::nullopt); }

} // namespace Autoclicker::Gui
