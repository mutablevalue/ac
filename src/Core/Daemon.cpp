#include "Core/Daemon.hpp"

#include "Core/Configuration.hpp"
#include "Utils/Logger.hpp"

#include <algorithm>
#include <array>
#include <cctype>
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
    : SocketPath(std::move(Path)),
      LeftScheduler(VirtualMouse, Types::MouseButton::Left),
      RightScheduler(VirtualMouse, Types::MouseButton::Right),
      Shutdown(LeftScheduler, RightScheduler) {}

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
    const auto Startup = ClickScheduler::Clock::now();
    LeftScheduler.configure(Config.Left, Startup);
    RightScheduler.configure(Config.Right, Startup);
    Hotkeys.configure(Config.Left.ToggleBinding, Config.Right.ToggleBinding, Config.ExitBinding);
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
                if ((Flags & (EPOLLHUP | EPOLLERR)) != 0U) {
                    finish_capture(Types::CaptureOutcome::Cancelled, {});
                    Shutdown.begin_shutdown();
                    break;
                }
                auto Message = Client.receive();
                if (!Message) {
                    finish_capture(Types::CaptureOutcome::Cancelled, {});
                    Shutdown.begin_shutdown();
                    break;
                }
                handle_message(*Message);
            } else if (Descriptor == Timer.get()) {
                auto Expirations = std::uint64_t{};
                static_cast<void>(read(Timer.get(), &Expirations, sizeof(Expirations)));
                const auto Now = ClickScheduler::Clock::now();
                if (CaptureDeadline && Now >= *CaptureDeadline) {
                    finish_capture(Types::CaptureOutcome::TimedOut, {});
                }
                if (FocusPollDeadline && Now >= *FocusPollDeadline) {
                    const auto WasAllowed = FocusAllowed;
                    static_cast<void>(refresh_focus(Now));
                    if (WasAllowed != FocusAllowed) send_status();
                }
                LeftScheduler.process(Now);
                RightScheduler.process(Now);
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
    // Hand the pointer back before the descriptors close rather than relying on kernel cleanup.
    static_cast<void>(InputListener.set_grab(false, false, false));
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
        {
            const auto Now = ClickScheduler::Clock::now();
            LeftScheduler.configure(Config.Left, Now);
            RightScheduler.configure(Config.Right, Now);
        }
        Hotkeys.configure(Config.Left.ToggleBinding, Config.Right.ToggleBinding, Config.ExitBinding);
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
        if (Message.Enabled) {
            Shutdown.set_enabled(Message.Button.value_or(Types::MouseButton::Left), *Message.Enabled, Now);
        }
        update_grab();
        static_cast<void>(Client.send({.Command = Types::IpcCommand::Ack}));
        arm_timer();
        break;
    }
    case Types::IpcCommand::BeginBindingCapture:
        begin_capture(Message.Capture ? Message.Capture->Token : 0U, ClickScheduler::Clock::now());
        static_cast<void>(Client.send({.Command = Types::IpcCommand::Ack}));
        break;
    case Types::IpcCommand::CancelBindingCapture:
        finish_capture(Types::CaptureOutcome::Cancelled, {});
        static_cast<void>(Client.send({.Command = Types::IpcCommand::Ack}));
        break;
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
    if (Event.Type == EV_SYN && Event.Code == SYN_DROPPED) {
        Hotkeys.reset();
        return;
    }
    if (Event.Type != EV_KEY) return;
    const auto Now = ClickScheduler::Clock::now();

    if (capture_active()) {
        // The click that opened the prompt is still in flight; it must not become the binding.
        if (CaptureArmedAt && Now < *CaptureArmedAt) return;
        if (Event.Value == 1 && Event.Code == KEY_ESC) {
            finish_capture(Types::CaptureOutcome::Cancelled, {});
            return;
        }
        if (const auto Chord = Hotkeys.capture(Event.Code, Event.Value)) {
            finish_capture(Types::CaptureOutcome::Captured, *Chord);
        }
        // An armed capture never dispatches a hotkey or feeds a scheduler.
        return;
    }

    if (Event.Kind == Types::InputDeviceKind::Mouse && (Event.Code == BTN_LEFT || Event.Code == BTN_RIGHT) &&
        (Event.Value == 0 || Event.Value == 1)) {
        if (Event.Value == 1) static_cast<void>(refresh_focus(Now));
        auto& Target = Event.Code == BTN_RIGHT ? RightScheduler : LeftScheduler;
        Target.on_physical_button(Event.Value == 1, Now);
    }
    // Mouse buttons are bindable too, so both kinds reach the hotkey matcher. The listener
    // guarantees one delivery per physical action.
    if (const auto Action = Hotkeys.process(Event.Code, Event.Value)) {
        switch (*Action) {
        case Types::HotkeyAction::ToggleLeftClicker:
            toggle_channel(Types::MouseButton::Left);
            break;
        case Types::HotkeyAction::ToggleRightClicker:
            toggle_channel(Types::MouseButton::Right);
            break;
        case Types::HotkeyAction::ExitAutoclicker:
            request_exit();
            break;
        }
    }
    arm_timer();
}

auto Daemon::capture_active() const noexcept -> bool { return CaptureDeadline.has_value(); }

auto Daemon::begin_capture(const std::uint32_t Token, const ClickScheduler::TimePoint Now) -> void {
    CaptureToken = Token;
    CaptureArmedAt = Now + std::chrono::milliseconds{300};
    CaptureDeadline = Now + std::chrono::seconds{10};
    // A running clicker during capture is chaotic, and binding a mouse button needs a clean click.
    Shutdown.set_enabled(Types::MouseButton::Left, false, Now);
    Shutdown.set_enabled(Types::MouseButton::Right, false, Now);
    Hotkeys.reset();
    update_grab();
    arm_timer();
    send_status();
}

auto Daemon::finish_capture(const Types::CaptureOutcome Outcome, Types::KeyChord Chord) -> void {
    if (!capture_active()) return;
    const auto Token = CaptureToken;
    CaptureDeadline.reset();
    CaptureArmedAt.reset();
    CaptureToken = 0;
    Hotkeys.reset();
    if (Client) {
        static_cast<void>(Client.send({.Command = Types::IpcCommand::BindingCaptured,
            .Capture = Types::BindingCapture{.Token = Token, .Outcome = Outcome, .Chord = Chord}}));
    }
    update_grab();
    arm_timer();
    send_status();
}

auto Daemon::grab_required() const -> bool {
    if (Shutdown.shutting_down() || capture_active()) return false;
    // Releasing the grab whenever output is disallowed is what keeps the FastClicker window
    // clickable while a hold channel is armed.
    if (!FocusAllowed) return false;
    return (Config.Left.Mode == Types::OperatingMode::Hold && Shutdown.enabled(Types::MouseButton::Left)) ||
           (Config.Right.Mode == Types::OperatingMode::Hold && Shutdown.enabled(Types::MouseButton::Right));
}

auto Daemon::update_grab() -> void {
    // Derived rather than counted, so the two channels can never leave the grab unbalanced.
    const auto Required = grab_required();
    const auto SwallowLeft = Required && Config.Left.Mode == Types::OperatingMode::Hold &&
                             Shutdown.enabled(Types::MouseButton::Left);
    const auto SwallowRight = Required && Config.Right.Mode == Types::OperatingMode::Hold &&
                              Shutdown.enabled(Types::MouseButton::Right);
    if (auto Result = InputListener.set_grab(Required, SwallowLeft, SwallowRight); !Result) {
        LatestError = Result.error().describe();
    }
}

auto Daemon::toggle_channel(const Types::MouseButton Button) -> void {
    const auto EnableRequested = !Shutdown.enabled(Button);
    const auto Now = ClickScheduler::Clock::now();
    static_cast<void>(refresh_focus(Now));
    Shutdown.set_enabled(Button, EnableRequested && VirtualMouse.ready(), Now);
    update_grab();
    arm_timer();
    send_status();
}

auto Daemon::channel_status(const ClickScheduler& Scheduler, const ClickScheduler::TimePoint Now) const
    -> Types::ChannelStatus {
    return {.Enabled = Scheduler.enabled(), .Mode = Scheduler.mode(),
            .PhysicalCps = Scheduler.physical_cps(Now), .EmittedCps = Scheduler.emitted_cps(Now),
            .AdditiveGateOpen = Scheduler.additive_gate_open()};
}

auto Daemon::status() const -> Types::DaemonStatus {
    const auto Now = ClickScheduler::Clock::now();
    return {.Lifecycle = Shutdown.state(),
            .Left = channel_status(LeftScheduler, Now),
            .Right = channel_status(RightScheduler, Now),
            .MouseConnected = InputListener.mouse_connected(),
            .FocusTrackingSupported = FocusState.Supported,
            .FocusObservable = FocusState.FocusObservable,
            .FocusAllowed = FocusAllowed,
            .ActiveApplication = ActiveApplication,
            .LatestError = LatestError,
            .CaptureActive = capture_active()};
}

auto Daemon::matches_target(const std::string_view Identity, const std::string_view WindowClass) const -> bool {
    if (!Config.TargetApplication) return true;
    const auto& Target = *Config.TargetApplication;
    // A target saved before process identities existed holds an X11 window class, so accept either
    // key. Both are case-insensitive because WM_CLASS capitalisation is purely a toolkit choice.
    const auto Same = [&Target](const std::string_view Value) {
        return !Value.empty() && Value.size() == Target.size() &&
            std::ranges::equal(Value, Target, {}, [](const char Character) {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
            }, [](const char Character) {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
            });
    };
    return Same(Identity) || Same(WindowClass);
}

auto Daemon::target_focused(const ClickScheduler::TimePoint Now) -> bool {
    if (FocusState.FocusObservable) {
        const auto Identity = Inventory.identify(FocusState.ActiveProcessId);
        ActiveApplication = !Identity.empty() ? Inventory.display_name(Identity)
                                              : FocusState.ActiveWindowClass;
        return matches_target(Identity, FocusState.ActiveWindowClass);
    }
    // A Wayland-native client holds focus. Nothing on this session can name it, so fall back to the
    // strongest signal that remains: whether the target is running at all. The GUI says so plainly.
    ActiveApplication.clear();
    if (!Config.TargetApplication) return true;
    Inventory.refresh_if_stale(Now);
    return Inventory.running(*Config.TargetApplication);
}

auto Daemon::refresh_focus(const ClickScheduler::TimePoint Now) -> bool {
    FocusState = FocusTracker.focus_state(OwnWindowId);
    const auto Allowed = FocusState.Supported && !FocusState.OwnWindowFocused && target_focused(Now);
    FocusAllowed = Allowed;
    // Only poll while the answer can change without an X11 event reaching us.
    FocusPollDeadline = FocusState.Supported && !FocusState.FocusObservable && Config.TargetApplication
        ? std::optional{Now + std::chrono::seconds{1}}
        : std::nullopt;
    LeftScheduler.set_output_allowed(Allowed, Now);
    RightScheduler.set_output_allowed(Allowed, Now);
    update_grab();
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
        auto Deadline = LeftScheduler.next_deadline();
        const auto Fold = [&Deadline](const std::optional<ClickScheduler::TimePoint>& Other) {
            if (!Other) return;
            Deadline = Deadline ? std::optional{std::min(*Deadline, *Other)} : Other;
        };
        Fold(RightScheduler.next_deadline());
        Fold(CaptureDeadline);
        Fold(FocusPollDeadline);
        if (Deadline) {
            const auto Nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(Deadline->time_since_epoch()).count();
            Specification.it_value.tv_sec = static_cast<time_t>(Nanoseconds / 1'000'000'000LL);
            Specification.it_value.tv_nsec = static_cast<long>(Nanoseconds % 1'000'000'000LL);
        }
    }
    static_cast<void>(timerfd_settime(Timer.get(), TFD_TIMER_ABSTIME, &Specification, nullptr));
}

} // namespace Autoclicker::Core
