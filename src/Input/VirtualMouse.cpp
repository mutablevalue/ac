#include "Input/VirtualMouse.hpp"

#include <cerrno>
#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>

namespace Autoclicker::Input {

VirtualMouse::~VirtualMouse() {
    if (Device != nullptr) libevdev_uinput_destroy(Device);
}

auto VirtualMouse::initialize() -> std::expected<void, Utils::Error> {
    auto* Definition = libevdev_new();
    if (Definition == nullptr) {
        return std::unexpected{Utils::Error{Types::ErrorCode::Internal, "Unable to allocate virtual mouse definition"}};
    }
    libevdev_set_name(Definition, "FastClicker Virtual Mouse");
    libevdev_set_id_bustype(Definition, BUS_USB);
    libevdev_set_id_vendor(Definition, 0xFACU);
    libevdev_set_id_product(Definition, 0x0001U);
    libevdev_set_id_version(Definition, 0x0001U);
    auto Result = libevdev_enable_event_type(Definition, EV_KEY);
    if (Result >= 0) Result = libevdev_enable_event_code(Definition, EV_KEY, BTN_LEFT, nullptr);
    if (Result >= 0) Result = libevdev_enable_event_type(Definition, EV_REL);
    if (Result >= 0) Result = libevdev_enable_event_code(Definition, EV_REL, REL_X, nullptr);
    if (Result >= 0) Result = libevdev_enable_event_code(Definition, EV_REL, REL_Y, nullptr);
    if (Result >= 0) Result = libevdev_uinput_create_from_device(
        Definition, LIBEVDEV_UINPUT_OPEN_MANAGED, &Device);
    libevdev_free(Definition);
    if (Result < 0) {
        const auto Code = Result == -EACCES ? Types::ErrorCode::PermissionDenied : Types::ErrorCode::DeviceUnavailable;
        return std::unexpected{Utils::Error{Code, "Unable to create /dev/uinput virtual mouse",
            std::error_code{-Result, std::generic_category()}}};
    }
    return {};
}

auto VirtualMouse::emit(const Types::ClickTransition Transition) -> bool {
    if (Device == nullptr) return false;
    const auto Value = Transition == Types::ClickTransition::Press ? 1 : 0;
    if (libevdev_uinput_write_event(Device, EV_KEY, BTN_LEFT, Value) < 0) return false;
    return libevdev_uinput_write_event(Device, EV_SYN, SYN_REPORT, 0) >= 0;
}

auto VirtualMouse::ready() const noexcept -> bool { return Device != nullptr; }

} // namespace Autoclicker::Input
