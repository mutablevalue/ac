#include "Input/InputListener.hpp"

#include "Input/DeviceGrab.hpp"
#include "Utils/UniqueFd.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <libudev.h>
#include <memory>
#include <ranges>
#include <string_view>
#include <sys/epoll.h>
#include <unordered_map>
#include <unordered_set>
#include <unistd.h>
#include <utility>

namespace Autoclicker::Input {
namespace {
auto input_error(const std::string& Message) -> Utils::Error {
    return {errno == EACCES ? Types::ErrorCode::PermissionDenied : Types::ErrorCode::DeviceUnavailable,
            Message, std::error_code{errno, std::generic_category()}};
}

auto property_true(udev_device* Device, const char* Name) -> bool {
    const auto* Value = udev_device_get_property_value(Device, Name);
    return Value != nullptr && std::string_view{Value} == "1";
}

auto stable_id(udev_device* Device) -> std::string {
    if (const auto* Path = udev_device_get_property_value(Device, "ID_PATH"); Path != nullptr) {
        if (const auto* Serial = udev_device_get_property_value(Device, "ID_SERIAL"); Serial != nullptr) {
            return std::string{Serial} + "|" + Path;
        }
        return Path;
    }
    if (const auto* Serial = udev_device_get_property_value(Device, "ID_SERIAL"); Serial != nullptr) {
        return Serial;
    }
    const auto* Syspath = udev_device_get_syspath(Device);
    return Syspath == nullptr ? std::string{} : std::string{Syspath};
}

// Every uinput node this process creates carries this in its name, which is how its own hotplug
// events are told apart from real hardware.
constexpr auto OwnDeviceMarker = std::string_view{"FastClicker"};

auto name_is_own(const char* Name) -> bool {
    return Name != nullptr && std::string_view{Name}.find(OwnDeviceMarker) != std::string_view::npos;
}

auto device_group(udev_device* Device) -> std::string {
    if (const auto* Group = udev_device_get_property_value(Device, "LIBINPUT_DEVICE_GROUP");
        Group != nullptr) {
        return Group;
    }
    return stable_id(Device);
}
} // namespace

class InputListener::Impl final {
public:
    struct DeviceEntry final {
        Utils::UniqueFd DescriptorValue;
        libevdev* DeviceHandle{};
        Types::InputDeviceInfo DeviceInfo;
        std::string MouseGroup;
        int MousePriority{};
        bool KeyboardCapable{};
        bool MouseButtonSource{};
        // Borrowed from Impl::Grabs, which outlives the entry across a rescan.
        DeviceGrab* Grab{};

        DeviceEntry(Utils::UniqueFd Descriptor, libevdev* Device, Types::InputDeviceInfo Info,
                    std::string Group, const int Priority, const bool HasKeyboard)
            : DescriptorValue(std::move(Descriptor)), DeviceHandle(Device), DeviceInfo(std::move(Info)),
              MouseGroup(std::move(Group)), MousePriority(Priority), KeyboardCapable(HasKeyboard) {}
        ~DeviceEntry() { if (DeviceHandle != nullptr) libevdev_free(DeviceHandle); }
        DeviceEntry(const DeviceEntry&) = delete;
        auto operator=(const DeviceEntry&) -> DeviceEntry& = delete;
        DeviceEntry(DeviceEntry&& Other) noexcept
            : DescriptorValue(std::move(Other.DescriptorValue)),
              DeviceHandle(std::exchange(Other.DeviceHandle, nullptr)), DeviceInfo(std::move(Other.DeviceInfo)),
              MouseGroup(std::move(Other.MouseGroup)), MousePriority(Other.MousePriority),
              KeyboardCapable(Other.KeyboardCapable), MouseButtonSource(Other.MouseButtonSource),
              Grab(std::exchange(Other.Grab, nullptr)) {}
        auto operator=(DeviceEntry&& Other) noexcept -> DeviceEntry& {
            if (this != &Other) {
                if (DeviceHandle != nullptr) libevdev_free(DeviceHandle);
                DescriptorValue = std::move(Other.DescriptorValue);
                DeviceHandle = std::exchange(Other.DeviceHandle, nullptr);
                DeviceInfo = std::move(Other.DeviceInfo);
                MouseGroup = std::move(Other.MouseGroup);
                MousePriority = Other.MousePriority;
                KeyboardCapable = Other.KeyboardCapable;
                MouseButtonSource = Other.MouseButtonSource;
                Grab = std::exchange(Other.Grab, nullptr);
            }
            return *this;
        }
    };

    Impl() : Context(udev_new(), &udev_unref),
             Monitor(nullptr, &udev_monitor_unref),
             Epoll(epoll_create1(EPOLL_CLOEXEC)) {}

    auto initialize() -> std::expected<void, Utils::Error> {
        if (!Context || !Epoll) return std::unexpected{input_error("Unable to initialize input discovery")};
        Monitor.reset(udev_monitor_new_from_netlink(Context.get(), "udev"));
        if (!Monitor || udev_monitor_filter_add_match_subsystem_devtype(Monitor.get(), "input", nullptr) < 0 ||
            udev_monitor_enable_receiving(Monitor.get()) < 0) {
            return std::unexpected{input_error("Unable to monitor input hotplug events")};
        }
        MonitorFd = udev_monitor_get_fd(Monitor.get());
        auto Event = epoll_event{.events = EPOLLIN, .data = {.fd = MonitorFd}};
        if (epoll_ctl(Epoll.get(), EPOLL_CTL_ADD, MonitorFd, &Event) < 0) {
            return std::unexpected{input_error("Unable to monitor udev descriptor")};
        }
        return rescan();
    }

    // True for the uinput nodes this process creates, so they are never mistaken for real
    // hardware. Used while enumerating, where the device name can be read back directly.
    [[nodiscard]] auto own_device(const char* Node, const char* Name) const -> bool {
        if (name_is_own(Name)) return true;
        if (Node == nullptr) return false;
        return std::ranges::any_of(Grabs, [Node](const auto& Pair) {
            const auto Passthrough = Pair.second->passthrough_node();
            return !Passthrough.empty() && Passthrough == Node;
        });
    }

    // The same question for a hotplug event, which cannot be answered the same way: NAME lives on
    // the parent input device and never on its event child, and by the time a remove event
    // arrives the sysfs entries behind both are already gone. Nodes this process created are
    // therefore remembered by path until their own remove event retires them.
    [[nodiscard]] auto own_device(udev_device* Device) -> bool {
        const auto* Action = udev_device_get_action(Device);
        const auto Removing = Action != nullptr && std::string_view{Action} == "remove";
        if (const auto* Node = udev_device_get_devnode(Device); Node != nullptr) {
            if (const auto Position = OwnNodes.find(Node); Position != OwnNodes.end()) {
                if (Removing) OwnNodes.erase(Position);
                return true;
            }
        }
        for (auto* Candidate = Device; Candidate != nullptr;
             Candidate = udev_device_get_parent(Candidate)) {
            const auto* Subsystem = udev_device_get_subsystem(Candidate);
            if (Subsystem == nullptr || std::string_view{Subsystem} != "input") break;
            if (name_is_own(udev_device_get_property_value(Candidate, "NAME"))) return true;
            if (name_is_own(udev_device_get_sysattr_value(Candidate, "name"))) return true;
        }
        return false;
    }

    // Records the clone nodes currently in existence. Called after anything that can engage a
    // grab, because DeviceGrab forgets its node the moment the clone is destroyed.
    auto remember_own_nodes() -> void {
        for (const auto& [Id, Grab] : Grabs) {
            if (const auto Node = Grab->passthrough_node(); !Node.empty()) {
                OwnNodes.emplace(Node);
            }
        }
    }

    auto dispatch(const DeviceEntry& Entry, const input_event& Event) -> void {
        // Anything the grab does not capture is relayed to the clone verbatim, including the
        // source's own EV_SYN frames, so multi-event frames stay intact.
        if (Entry.Grab != nullptr && Entry.Grab->engaged() &&
            !Entry.Grab->swallows(Event.type, Event.code)) {
            Entry.Grab->forward(Event.type, Event.code, Event.value);
        }
        if (!Callback) return;
        if (Event.type == EV_KEY && Event.code >= BTN_MISC && Event.code <= BTN_TASK) {
            if (!Entry.MouseButtonSource) return;
            Callback({.DeviceId = Entry.DeviceInfo.Id, .Kind = Types::InputDeviceKind::Mouse,
                      .Type = Event.type, .Code = Event.code, .Value = Event.value});
            return;
        }
        if (Entry.KeyboardCapable &&
            (Event.type == EV_KEY || (Event.type == EV_SYN && Event.code == SYN_DROPPED))) {
            Callback({.DeviceId = Entry.DeviceInfo.Id, .Kind = Types::InputDeviceKind::Keyboard,
                      .Type = Event.type, .Code = Event.code, .Value = Event.value});
        }
    }

    // The kernel dropped events, so held state is unknown. Release everything the clone still
    // holds and force every consumer to rebuild from the next real events.
    auto recover(const DeviceEntry& Entry) -> void {
        if (Entry.Grab != nullptr) Entry.Grab->release_all();
        if (!Callback) return;
        if (Entry.MouseButtonSource) {
            for (const auto Code : {BTN_LEFT, BTN_RIGHT}) {
                Callback({.DeviceId = Entry.DeviceInfo.Id, .Kind = Types::InputDeviceKind::Mouse,
                          .Type = EV_KEY, .Code = static_cast<std::uint16_t>(Code), .Value = 0});
            }
        }
        Callback({.DeviceId = Entry.DeviceInfo.Id, .Kind = Types::InputDeviceKind::Keyboard,
                  .Type = EV_SYN, .Code = SYN_DROPPED, .Value = 0});
    }

    auto rescan() -> std::expected<void, Utils::Error> {
        // Every descriptor is about to close, which drops any kernel grab with it.
        for (auto& [Id, Grab] : Grabs) Grab->invalidate();
        if (MouseConnected && Callback) {
            Callback({.Kind = Types::InputDeviceKind::Mouse,
                      .Type = EV_KEY, .Code = BTN_LEFT, .Value = 0});
            Callback({.Kind = Types::InputDeviceKind::Mouse,
                      .Type = EV_KEY, .Code = BTN_RIGHT, .Value = 0});
        }
        if (Callback) {
            Callback({.Kind = Types::InputDeviceKind::Keyboard,
                      .Type = EV_SYN, .Code = SYN_DROPPED, .Value = 0});
        }
        for (auto& Entry : Entries) static_cast<void>(epoll_ctl(Epoll.get(), EPOLL_CTL_DEL, Entry.DescriptorValue.get(), nullptr));
        Entries.clear();
        MouseConnected = false;
        auto PermissionFailure = false;

        auto Enumerate = std::unique_ptr<udev_enumerate, decltype(&udev_enumerate_unref)>{udev_enumerate_new(Context.get()), &udev_enumerate_unref};
        if (!Enumerate) return std::unexpected{input_error("Unable to enumerate input devices")};
        udev_enumerate_add_match_subsystem(Enumerate.get(), "input");
        udev_enumerate_scan_devices(Enumerate.get());

        udev_list_entry* Item{};
        udev_list_entry_foreach(Item, udev_enumerate_get_list_entry(Enumerate.get())) {
            const auto* Syspath = udev_list_entry_get_name(Item);
            auto Device = std::unique_ptr<udev_device, decltype(&udev_device_unref)>{
                udev_device_new_from_syspath(Context.get(), Syspath), &udev_device_unref};
            if (!Device) continue;
            const auto* Node = udev_device_get_devnode(Device.get());
            if (Node == nullptr || std::string_view{Node}.find("/event") == std::string_view::npos) continue;
            const auto IsMouse = property_true(Device.get(), "ID_INPUT_MOUSE");
            const auto IsKeyboard = property_true(Device.get(), "ID_INPUT_KEYBOARD");
            if (!IsMouse && !IsKeyboard) continue;
            auto Descriptor = Utils::UniqueFd{open(Node, O_RDONLY | O_NONBLOCK | O_CLOEXEC)};
            if (!Descriptor) {
                PermissionFailure = PermissionFailure || errno == EACCES;
                continue;
            }
            libevdev* Evdev{};
            if (libevdev_new_from_fd(Descriptor.get(), &Evdev) < 0) continue;
            const auto* DeviceName = libevdev_get_name(Evdev);
            const auto Name = DeviceName == nullptr ? std::string{Node} : std::string{DeviceName};
            if (own_device(Node, Name.c_str())) {
                libevdev_free(Evdev);
                continue;
            }
            auto Info = Types::InputDeviceInfo{.Id = stable_id(Device.get()), .Name = Name,
                .DeviceNode = Node, .Kind = IsMouse ? Types::InputDeviceKind::Mouse : Types::InputDeviceKind::Keyboard,
                .Connected = true};
            auto Event = epoll_event{.events = EPOLLIN, .data = {.fd = Descriptor.get()}};
            if (epoll_ctl(Epoll.get(), EPOLL_CTL_ADD, Descriptor.get(), &Event) < 0) {
                libevdev_free(Evdev);
                continue;
            }
            const auto HasLeftButton = IsMouse &&
                libevdev_has_event_code(Evdev, EV_KEY, BTN_LEFT) != 0;
            const auto HasRelativeMotion =
                libevdev_has_event_code(Evdev, EV_REL, REL_X) != 0 &&
                libevdev_has_event_code(Evdev, EV_REL, REL_Y) != 0;
            const auto MousePriority = HasLeftButton
                ? 1 + (HasRelativeMotion ? 2 : 0) + (!IsKeyboard ? 4 : 0)
                : 0;
            Entries.emplace_back(std::move(Descriptor), Evdev, std::move(Info),
                                 device_group(Device.get()), MousePriority, IsKeyboard);
        }

        // Composite USB mice commonly expose the same buttons on multiple interfaces.
        // Select one authoritative button source for each libinput physical-device group.
        auto BestMouseSources = std::unordered_map<std::string, std::size_t>{};
        for (auto Index = std::size_t{}; Index < Entries.size(); ++Index) {
            const auto& Entry = Entries[Index];
            if (Entry.MousePriority == 0) continue;
            const auto [Position, Inserted] = BestMouseSources.try_emplace(Entry.MouseGroup, Index);
            if (!Inserted) {
                const auto& Current = Entries[Position->second];
                if (Entry.MousePriority > Current.MousePriority ||
                    (Entry.MousePriority == Current.MousePriority &&
                     Entry.DeviceInfo.DeviceNode < Current.DeviceInfo.DeviceNode)) {
                    Position->second = Index;
                }
            }
        }
        for (const auto& Source : BestMouseSources) {
            Entries[Source.second].MouseButtonSource = true;
            MouseConnected = true;
        }

        // Carry each elected pointer's grab across the rescan so an unchanged device keeps its
        // passthrough node instead of tearing it down and raising more hotplug events.
        auto Surviving = std::unordered_map<std::string, std::unique_ptr<DeviceGrab>>{};
        for (auto& Entry : Entries) {
            if (!Entry.MouseButtonSource) continue;
            auto Existing = Grabs.find(Entry.DeviceInfo.Id);
            auto& Slot = Surviving[Entry.DeviceInfo.Id];
            Slot = Existing != Grabs.end() ? std::move(Existing->second)
                                           : std::make_unique<DeviceGrab>();
            Entry.Grab = Slot.get();
        }
        Grabs = std::move(Surviving);
        for (auto& Entry : Entries) {
            if (Entry.Grab == nullptr) continue;
            // Safe before attach because invalidate() already cleared the stale handle.
            static_cast<void>(Entry.Grab->request(GrabRequested, SwallowLeft, SwallowRight));
            static_cast<void>(
                Entry.Grab->attach(Entry.DeviceHandle, Entry.DeviceInfo.Name, Entry.DeviceInfo.Id));
        }
        remember_own_nodes();

        if (PermissionFailure) {
            return std::unexpected{Utils::Error{Types::ErrorCode::PermissionDenied,
                "The desktop session did not grant access to one or more selected input devices"}};
        }
        return {};
    }

    std::unique_ptr<udev, decltype(&udev_unref)> Context;
    std::unique_ptr<udev_monitor, decltype(&udev_monitor_unref)> Monitor;
    Utils::UniqueFd Epoll;
    int MonitorFd{-1};
    std::vector<DeviceEntry> Entries;
    std::unordered_map<std::string, std::unique_ptr<DeviceGrab>> Grabs;
    std::unordered_set<std::string> OwnNodes;
    EventCallback Callback;
    bool MouseConnected{};
    bool GrabRequested{};
    bool SwallowLeft{};
    bool SwallowRight{};
};

InputListener::InputListener() : Implementation(std::make_unique<Impl>()) {}
InputListener::~InputListener() = default;
InputListener::InputListener(InputListener&&) noexcept = default;
auto InputListener::operator=(InputListener&&) noexcept -> InputListener& = default;

auto InputListener::start(EventCallback Callback) -> std::expected<void, Utils::Error> {
    Implementation->Callback = std::move(Callback);
    return Implementation->initialize();
}

auto InputListener::drain() -> std::expected<void, Utils::Error> {
    auto Events = std::array<epoll_event, 32>{};
    const auto Count = epoll_wait(Implementation->Epoll.get(), Events.data(), static_cast<int>(Events.size()), 0);
    if (Count < 0 && errno != EINTR) return std::unexpected{input_error("Unable to poll input devices")};
    for (auto Index = 0; Index < Count; ++Index) {
        const auto Descriptor = Events[static_cast<std::size_t>(Index)].data.fd;
        if (Descriptor == Implementation->MonitorFd) {
            // Our own uinput nodes raise hotplug events. Rescanning on those would tear down the
            // grab that created the passthrough, which would raise another event, and so on.
            auto External = false;
            while (auto* Device = udev_monitor_receive_device(Implementation->Monitor.get())) {
                // Every event is classified, never short-circuited, so a remove always retires
                // its remembered node.
                const auto Own = Implementation->own_device(Device);
                External = External || !Own;
                udev_device_unref(Device);
            }
            if (!External) continue;
            return Implementation->rescan();
        }
        const auto Entry = std::ranges::find_if(Implementation->Entries,
            [Descriptor](const auto& Value) { return Value.DescriptorValue.get() == Descriptor; });
        if (Entry == Implementation->Entries.end()) continue;
        auto Event = input_event{};
        auto Status = libevdev_next_event(Entry->DeviceHandle, LIBEVDEV_READ_FLAG_NORMAL, &Event);
        while (Status == LIBEVDEV_READ_STATUS_SUCCESS || Status == LIBEVDEV_READ_STATUS_SYNC) {
            if (Status == LIBEVDEV_READ_STATUS_SYNC) {
                auto Delta = input_event{};
                while (libevdev_next_event(Entry->DeviceHandle, LIBEVDEV_READ_FLAG_SYNC, &Delta) ==
                       LIBEVDEV_READ_STATUS_SYNC) {
                }
                Implementation->recover(*Entry);
            } else {
                Implementation->dispatch(*Entry, Event);
            }
            Status = libevdev_next_event(Entry->DeviceHandle, LIBEVDEV_READ_FLAG_NORMAL, &Event);
        }
        // A deferred engage completes as soon as the device reports no held buttons.
        if (Entry->Grab != nullptr) {
            static_cast<void>(Entry->Grab->poll_pending());
            Implementation->remember_own_nodes();
        }
    }
    return {};
}

auto InputListener::set_grab(const bool Engage, const bool SwallowLeft, const bool SwallowRight)
    -> std::expected<void, Utils::Error> {
    Implementation->GrabRequested = Engage;
    Implementation->SwallowLeft = SwallowLeft;
    Implementation->SwallowRight = SwallowRight;
    auto Result = std::expected<void, Utils::Error>{};
    for (auto& [Id, Grab] : Implementation->Grabs) {
        if (auto Outcome = Grab->request(Engage, SwallowLeft, SwallowRight); !Outcome && Result) {
            Result = std::unexpected{Outcome.error()};
        }
    }
    Implementation->remember_own_nodes();
    return Result;
}

auto InputListener::descriptor() const noexcept -> int { return Implementation->Epoll.get(); }
auto InputListener::mouse_connected() const noexcept -> bool { return Implementation->MouseConnected; }

auto InputListener::grab_engaged() const noexcept -> bool {
    return std::ranges::any_of(Implementation->Grabs,
                               [](const auto& Pair) { return Pair.second->engaged(); });
}

} // namespace Autoclicker::Input
