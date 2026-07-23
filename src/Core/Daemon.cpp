#include "Core/Daemon.hpp"

#include "Core/Configuration.hpp"
#include "Utils/Logger.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace Autoclicker::Core {
namespace {
auto daemon_error(const std::string& Message) -> Utils::Error {
    return {Types::ErrorCode::Io, Message, std::error_code{errno, std::generic_category()}};
}

auto add_epoll(const int Epoll, const int Descriptor) -> bool {
    auto Event = epoll_event{.events = EPOLLIN | EPOLLHUP | EPOLLERR, .data = {.fd = Descriptor}};
    return epoll_ctl(Epoll, EPOLL_CTL_ADD, Descriptor, &Event) == 0;
}
} // namespace

Daemon::Daemon(std::filesystem::path Path)
    : SocketPath(std::move(Path)), Scheduler(VirtualMouse), Shutdown(Scheduler) {}

Daemon::~Daemon() {
    Shutdown.begin_shutdown();
    std::error_code ErrorValue;
    std::filesystem::remove(SocketPath, ErrorValue);
}

auto Daemon::initialize() -> std::expected<void, Utils::Error> {
    pthread_setname_np(pthread_self(), "autoclickerd");
    if (prctl(PR_SET_PDEATHSIG, SIGTERM) < 0) return std::unexpected{daemon_error("Unable to set parent-death signal")};
    if (getppid() == 1) return std::unexpected{Utils::Error{Types::ErrorCode::Process, "GUI parent exited during daemon startup"}};

    auto SignalMask = sigset_t{};
    sigemptyset(&SignalMask);
    sigaddset(&SignalMask, SIGINT);
    sigaddset(&SignalMask, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &SignalMask, nullptr) != 0) return std::unexpected{daemon_error("Unable to block shutdown signals")};
    Signals.reset(signalfd(-1, &SignalMask, SFD_NONBLOCK | SFD_CLOEXEC));
    Timer.reset(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC));
    Epoll.reset(epoll_create1(EPOLL_CLOEXEC));
    if (!Signals || !Timer || !Epoll) return std::unexpected{daemon_error("Unable to create daemon event descriptors")};

    const auto LockPath = SocketPath.string() + ".daemon.lock";
    std::error_code DirectoryError;
    std::filesystem::create_directories(SocketPath.parent_path(), DirectoryError);
    if (DirectoryError) return std::unexpected{Utils::Error{Types::ErrorCode::Io, "Unable to create runtime directory", DirectoryError}};
    static_cast<void>(chmod(SocketPath.parent_path().c_str(), 0700));
    InstanceLock.reset(open(LockPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600));
    if (!InstanceLock || flock(InstanceLock.get(), LOCK_EX | LOCK_NB) < 0) {
        return std::unexpected{Utils::Error{Types::ErrorCode::Process, "Another autoclicker daemon is already running"}};
    }
    std::error_code FileError;
    std::filesystem::remove(SocketPath, FileError);
    auto Listening = Ipc::Socket::listen(SocketPath);
    if (!Listening) return std::unexpected{Listening.error()};
    Server = std::move(*Listening);

    if (auto Result = VirtualMouse.initialize(); !Result) LatestError = Result.error().describe();
    Config = Configuration::instance().snapshot();
    Scheduler.configure(Config, ClickScheduler::Clock::now());
    Hotkeys.configure(Config.ToggleBinding, Config.ExitBinding);
    if (auto Result = FocusTracker.initialize(); !Result) LatestError = Result.error().describe();
    static_cast<void>(refresh_focus(ClickScheduler::Clock::now()));
    if (auto Result = InputListener.start(
            [this](const Types::RawInputEvent& Event) { handle_input(Event); }); !Result) {
        LatestError = Result.error().describe();
    }

    if (!add_epoll(Epoll.get(), Server.descriptor()) || !add_epoll(Epoll.get(), Timer.get()) ||
        !add_epoll(Epoll.get(), Signals.get())) {
        return std::unexpected{daemon_error("Unable to register daemon descriptors")};
    }
    if (InputListener.descriptor() >= 0) static_cast<void>(add_epoll(Epoll.get(), InputListener.descriptor()));
    if (FocusTracker.descriptor() >= 0) static_cast<void>(add_epoll(Epoll.get(), FocusTracker.descriptor()));
    return {};
}

auto Daemon::run() -> std::expected<void, Utils::Error> {
    if (auto Result = initialize(); !Result) return Result;
    auto Events = std::array<epoll_event, 16>{};
    while (!Shutdown.shutting_down()) {
        const auto Count = epoll_wait(Epoll.get(), Events.data(), static_cast<int>(Events.size()), -1);
        if (Count < 0) {
            if (errno == EINTR) continue;
            return std::unexpected{daemon_error("Daemon event loop failed")};
        }
        for (auto Index = 0; Index < Count && !Shutdown.shutting_down(); ++Index) {
            const auto Descriptor = Events[static_cast<std::size_t>(Index)].data.fd;
            const auto Flags = Events[static_cast<std::size_t>(Index)].events;
            if (Descriptor == Server.descriptor()) {
                auto Accepted = Server.accept_same_user();
                if (!Accepted) { LatestError = Accepted.error().describe(); continue; }
                Client = std::move(*Accepted);
                static_cast<void>(add_epoll(Epoll.get(), Client.descriptor()));
            } else if (Client && Descriptor == Client.descriptor()) {
                if ((Flags & (EPOLLHUP | EPOLLERR)) != 0U) { Shutdown.begin_shutdown(); break; }
                auto Message = Client.receive();
                if (!Message) { Shutdown.begin_shutdown(); break; }
                handle_message(*Message);
            } else if (Descriptor == Timer.get()) {
                auto Expirations = std::uint64_t{};
                static_cast<void>(read(Timer.get(), &Expirations, sizeof(Expirations)));
                Scheduler.process(ClickScheduler::Clock::now());
                arm_timer();
            } else if (Descriptor == Signals.get()) {
                auto Signal = signalfd_siginfo{};
                static_cast<void>(read(Signals.get(), &Signal, sizeof(Signal)));
                Shutdown.begin_shutdown();
            } else if (Descriptor == InputListener.descriptor()) {
                if (auto Result = InputListener.drain(); !Result) LatestError = Result.error().describe();
            } else if (Descriptor == FocusTracker.descriptor()) {
                if (FocusTracker.drain_changes()) {
                    static_cast<void>(refresh_focus(ClickScheduler::Clock::now()));
                    arm_timer();
                    send_status();
                }
            }
        }
    }
    Shutdown.begin_shutdown();
    arm_timer();
    Shutdown.mark_stopped();
    return {};
}

auto Daemon::handle_message(const Types::IpcMessage& Message) -> void {
    if (Message.Version != Types::IpcMessage::ProtocolVersion) {
        static_cast<void>(Client.send({.Command = Types::IpcCommand::Error, .Message = "Protocol version mismatch"}));
        return;
    }
    switch (Message.Command) {
    case Types::IpcCommand::Hello:
        static_cast<void>(Client.send({.Command = Types::IpcCommand::Ack}));
        break;
    case Types::IpcCommand::SetConfiguration:
        if (!Message.Configuration) break;
        if (auto Result = Configuration::validate(*Message.Configuration); !Result) {
            static_cast<void>(Client.send({.Command = Types::IpcCommand::Error, .Message = Result.error().describe()}));
            break;
        }
        Config = *Message.Configuration;
        Scheduler.configure(Config, ClickScheduler::Clock::now());
        Hotkeys.configure(Config.ToggleBinding, Config.ExitBinding);
        static_cast<void>(refresh_focus(ClickScheduler::Clock::now()));
        static_cast<void>(Client.send({.Command = Types::IpcCommand::Ack}));
        arm_timer();
        break;
    case Types::IpcCommand::SetOwnWindow:
        if (Message.WindowId) OwnWindowId = *Message.WindowId;
        static_cast<void>(refresh_focus(ClickScheduler::Clock::now()));
        static_cast<void>(Client.send({.Command = Types::IpcCommand::Ack}));
        arm_timer();
        break;
    case Types::IpcCommand::SetEnabled: {
        if (Message.Enabled && *Message.Enabled && !VirtualMouse.ready()) {
            static_cast<void>(Client.send({.Command = Types::IpcCommand::Error,
                .Message = LatestError.empty() ? "Virtual mouse is unavailable" : LatestError}));
            break;
        }
        const auto Now = ClickScheduler::Clock::now();
        static_cast<void>(refresh_focus(Now));
        if (Message.Enabled) Shutdown.set_enabled(*Message.Enabled, Now);
        static_cast<void>(Client.send({.Command = Types::IpcCommand::Ack}));
        arm_timer();
        break;
    }
    case Types::IpcCommand::GetStatus:
        static_cast<void>(refresh_focus(ClickScheduler::Clock::now()));
        send_status();
        break;
    case Types::IpcCommand::Shutdown:
        static_cast<void>(Client.send({.Command = Types::IpcCommand::Ack}));
        Shutdown.begin_shutdown();
        break;
    default:
        static_cast<void>(Client.send({.Command = Types::IpcCommand::Error, .Message = "Unsupported daemon command"}));
        break;
    }
}

auto Daemon::handle_input(const Types::RawInputEvent& Event) -> void {
    if (Event.Kind == Types::InputDeviceKind::Keyboard && Event.Type == EV_SYN && Event.Code == SYN_DROPPED) {
        Hotkeys.reset();
        return;
    }
    if (Event.Type != EV_KEY) return;
    if (Event.Kind == Types::InputDeviceKind::Mouse && Event.Code == BTN_LEFT && (Event.Value == 0 || Event.Value == 1)) {
        const auto Now = ClickScheduler::Clock::now();
        if (Event.Value == 1) static_cast<void>(refresh_focus(Now));
        Scheduler.on_physical_button(Event.Value == 1, Now);
        arm_timer();
    }
    if (Event.Kind == Types::InputDeviceKind::Keyboard) if (const auto Action = Hotkeys.process(Event.Code, Event.Value)) {
        if (*Action == Types::HotkeyAction::ToggleAutoclicker) {
            const auto EnableRequested = !Scheduler.enabled();
            const auto Now = ClickScheduler::Clock::now();
            static_cast<void>(refresh_focus(Now));
            if (EnableRequested && !VirtualMouse.ready()) {
                Shutdown.set_enabled(false, Now);
            } else {
                Shutdown.set_enabled(EnableRequested, Now);
            }
            arm_timer();
            send_status();
        } else {
            request_exit();
        }
    }
}

auto Daemon::status() const -> Types::DaemonStatus {
    const auto Now = ClickScheduler::Clock::now();
    return {.Lifecycle = Shutdown.state(), .Mode = Config.Mode,
            .MouseConnected = InputListener.mouse_connected(),
            .FocusTrackingSupported = FocusState.Supported,
            .FocusAllowed = FocusState.Supported && !FocusState.OwnWindowFocused && FocusState.TargetFocused,
            .PhysicalCps = Scheduler.physical_cps(Now), .EmittedCps = Scheduler.emitted_cps(Now),
            .ActiveApplication = FocusState.ActiveApplication,
            .LatestError = LatestError};
}

auto Daemon::refresh_focus(const ClickScheduler::TimePoint Now) -> bool {
    FocusState = FocusTracker.focus_state(Config.TargetApplication, OwnWindowId);
    const auto Allowed = FocusState.Supported && !FocusState.OwnWindowFocused && FocusState.TargetFocused;
    Scheduler.set_output_allowed(Allowed, Now);
    return Allowed;
}

auto Daemon::send_status() -> void {
    if (Client) static_cast<void>(Client.send({.Command = Types::IpcCommand::Status, .Status = status()}));
}

auto Daemon::request_exit() -> void {
    Shutdown.begin_shutdown();
    if (Client) static_cast<void>(Client.send({.Command = Types::IpcCommand::ExitRequested}));
}

auto Daemon::arm_timer() -> void {
    auto Specification = itimerspec{};
    if (!Shutdown.shutting_down()) {
        const auto Deadline = Scheduler.next_deadline();
        if (Deadline) {
            const auto Nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(Deadline->time_since_epoch()).count();
            Specification.it_value.tv_sec = static_cast<time_t>(Nanoseconds / 1'000'000'000LL);
            Specification.it_value.tv_nsec = static_cast<long>(Nanoseconds % 1'000'000'000LL);
        }
    }
    static_cast<void>(timerfd_settime(Timer.get(), TFD_TIMER_ABSTIME, &Specification, nullptr));
}

} // namespace Autoclicker::Core
