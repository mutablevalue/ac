#include "Input/InputListener.hpp"

#include "Utils/UniqueFd.hpp"

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
              KeyboardCapable(Other.KeyboardCapable), MouseButtonSource(Other.MouseButtonSource) {}
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

    auto rescan() -> std::expected<void, Utils::Error> {
        if (MouseConnected && Callback) {
            Callback({.Kind = Types::InputDeviceKind::Mouse,
                      .Type = EV_KEY, .Code = BTN_LEFT, .Value = 0});
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
            if (Name == "FastClicker Virtual Mouse") {
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
    EventCallback Callback;
    bool MouseConnected{};
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
            while (auto* Device = udev_monitor_receive_device(Implementation->Monitor.get())) udev_device_unref(Device);
            return Implementation->rescan();
        }
        const auto Entry = std::ranges::find_if(Implementation->Entries,
            [Descriptor](const auto& Value) { return Value.DescriptorValue.get() == Descriptor; });
        if (Entry == Implementation->Entries.end()) continue;
        auto Event = input_event{};
        while (libevdev_next_event(Entry->DeviceHandle, LIBEVDEV_READ_FLAG_NORMAL, &Event) == LIBEVDEV_READ_STATUS_SUCCESS) {
            if (Implementation->Callback) {
                if (Event.type == EV_KEY && Event.code == BTN_LEFT) {
                    if (!Entry->MouseButtonSource) continue;
                    Implementation->Callback({.DeviceId = Entry->DeviceInfo.Id,
                        .Kind = Types::InputDeviceKind::Mouse, .Type = Event.type,
                        .Code = Event.code, .Value = Event.value});
                } else if (Entry->KeyboardCapable &&
                           (Event.type == EV_KEY ||
                            (Event.type == EV_SYN && Event.code == SYN_DROPPED))) {
                    Implementation->Callback({.DeviceId = Entry->DeviceInfo.Id,
                        .Kind = Types::InputDeviceKind::Keyboard, .Type = Event.type,
                        .Code = Event.code, .Value = Event.value});
                }
            }
        }
    }
    return {};
}

auto InputListener::descriptor() const noexcept -> int { return Implementation->Epoll.get(); }
auto InputListener::mouse_connected() const noexcept -> bool { return Implementation->MouseConnected; }

} // namespace Autoclicker::Input
