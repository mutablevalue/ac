#include "Input/DeviceGrab.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <utility>

namespace Autoclicker::Input {
namespace {
auto grab_error(const char* Message) -> Utils::Error {
    return {errno == EACCES ? Types::ErrorCode::PermissionDenied : Types::ErrorCode::DeviceUnavailable,
            Message, std::error_code{errno, std::generic_category()}};
}
} // namespace

DeviceGrab::~DeviceGrab() { detach(); }

auto DeviceGrab::attach(libevdev* NewDevice, std::string Name, std::string NewIdentity)
    -> std::expected<void, Utils::Error> {
    // The same physical device keeps its clone across a rescan so hotplug churn cannot turn into
    // a create/destroy storm on the uinput node.
    const auto SameDevice = Passthrough != nullptr && !Identity.empty() && NewIdentity == Identity;
    if (!SameDevice) {
        if (Passthrough != nullptr) {
            libevdev_uinput_destroy(Passthrough);
            Passthrough = nullptr;
            PassthroughNode.clear();
            PassthroughName.clear();
        }
    }
    // Whatever descriptor held the previous grab is already closed, so the kernel dropped it.
    Engaged = false;
    ForwardedDown.fill(false);
    Device = NewDevice;
    DeviceName = std::move(Name);
    Identity = std::move(NewIdentity);
    if (!Requested) return {};
    return engage();
}

auto DeviceGrab::detach() -> void {
    disengage();
    Device = nullptr;
    Identity.clear();
    DeviceName.clear();
}

auto DeviceGrab::invalidate() -> void {
    Device = nullptr;
    Engaged = false;
    ForwardedDown.fill(false);
}

auto DeviceGrab::request(const bool Engage, const bool SwallowLeft, const bool SwallowRight)
    -> std::expected<void, Utils::Error> {
    SwallowLeftButton = SwallowLeft;
    SwallowRightButton = SwallowRight;
    if (!Engage) {
        Requested = false;
        disengage();
        return {};
    }
    Requested = true;
    if (Engaged) return {};
    return engage();
}

auto DeviceGrab::poll_pending() -> std::expected<void, Utils::Error> {
    if (!Requested || Engaged) return {};
    return engage();
}

auto DeviceGrab::device_idle() const -> bool {
    if (Device == nullptr) return false;
    auto Bits = std::array<unsigned char, (KEY_MAX + 7) / 8>{};
    const auto Request = static_cast<unsigned long>(EVIOCGKEY(Bits.size()));
    if (ioctl(libevdev_get_fd(Device), Request, Bits.data()) < 0) {
        // State is unreadable, which in practice means the node is going away. Let the grab
        // attempt fail and surface a real error rather than deferring forever.
        return true;
    }
    return std::ranges::all_of(Bits, [](const unsigned char Value) { return Value == 0; });
}

auto DeviceGrab::engage() -> std::expected<void, Utils::Error> {
    if (Device == nullptr || Engaged) return {};
    // Engaging while a button is held would swallow its release and strand the button in
    // whichever application saw the press on the real node.
    if (!device_idle()) return {};

    if (Passthrough == nullptr) {
        // libevdev_uinput_create_from_device copies the name verbatim, so an untouched clone
        // would be indistinguishable from the real mouse and the listener would re-enumerate it.
        PassthroughName = "FastClicker Passthrough " + DeviceName;
        libevdev_set_name(Device, PassthroughName.c_str());
        const auto Created = libevdev_uinput_create_from_device(
            Device, LIBEVDEV_UINPUT_OPEN_MANAGED, &Passthrough);
        libevdev_set_name(Device, DeviceName.c_str());
        if (Created < 0) {
            Passthrough = nullptr;
            PassthroughName.clear();
            errno = -Created;
            return std::unexpected{grab_error("Unable to create the passthrough mouse")};
        }
        const auto* Node = libevdev_uinput_get_devnode(Passthrough);
        PassthroughNode = Node == nullptr ? std::string{} : std::string{Node};
    }

    if (libevdev_grab(Device, LIBEVDEV_GRAB) < 0) {
        libevdev_uinput_destroy(Passthrough);
        Passthrough = nullptr;
        PassthroughNode.clear();
        PassthroughName.clear();
        return std::unexpected{grab_error("Another process is already holding this mouse")};
    }
    ForwardedDown.fill(false);
    Engaged = true;
    return {};
}

auto DeviceGrab::disengage() -> void {
    if (Engaged) {
        // Release through the clone before the real node comes back, so the only artefact of
        // disengaging is a spurious release that libinput discards.
        release_all();
        if (Device != nullptr) static_cast<void>(libevdev_grab(Device, LIBEVDEV_UNGRAB));
        Engaged = false;
    }
    if (Passthrough != nullptr) {
        libevdev_uinput_destroy(Passthrough);
        Passthrough = nullptr;
    }
    PassthroughNode.clear();
    PassthroughName.clear();
}

auto DeviceGrab::engaged() const noexcept -> bool { return Engaged; }

auto DeviceGrab::swallows(const std::uint16_t Type, const std::uint16_t Code) const noexcept -> bool {
    if (Type != EV_KEY) return false;
    return (SwallowLeftButton && Code == BTN_LEFT) || (SwallowRightButton && Code == BTN_RIGHT);
}

auto DeviceGrab::forward(const std::uint16_t Type, const std::uint16_t Code,
                         const std::int32_t Value) -> void {
    if (Passthrough == nullptr) return;
    if (Type == EV_KEY && Code < ForwardedDown.size() && (Value == 0 || Value == 1)) {
        ForwardedDown[Code] = Value == 1;
    }
    // The source's own EV_SYN frames are relayed as-is, so multi-event frames stay intact.
    static_cast<void>(libevdev_uinput_write_event(Passthrough, Type, Code, Value));
}

auto DeviceGrab::release_all() -> void {
    if (Passthrough == nullptr) return;
    auto Released = false;
    for (auto Index = std::size_t{}; Index < ForwardedDown.size(); ++Index) {
        if (!ForwardedDown[Index]) continue;
        ForwardedDown[Index] = false;
        static_cast<void>(libevdev_uinput_write_event(
            Passthrough, EV_KEY, static_cast<unsigned int>(Index), 0));
        Released = true;
    }
    if (Released) static_cast<void>(libevdev_uinput_write_event(Passthrough, EV_SYN, SYN_REPORT, 0));
}

auto DeviceGrab::passthrough_node() const noexcept -> std::string_view { return PassthroughNode; }
auto DeviceGrab::passthrough_name() const noexcept -> std::string_view { return PassthroughName; }

} // namespace Autoclicker::Input
