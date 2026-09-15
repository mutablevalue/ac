#pragma once

#include "Utils/Error.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

struct libevdev;
struct libevdev_uinput;

namespace Autoclicker::Input {

// Takes exclusive ownership of one physical pointer so a held button can drive a click stream
// instead of sitting logically down at the compositor. Everything the grab swallows that is not
// the captured button is relayed verbatim through a uinput clone of the same device, so motion,
// scrolling and the remaining buttons keep working while engaged.
class DeviceGrab final {
public:
    DeviceGrab() = default;
    ~DeviceGrab();
    DeviceGrab(const DeviceGrab&) = delete;
    auto operator=(const DeviceGrab&) -> DeviceGrab& = delete;

    // Binds to the elected pointer after each rescan. An unchanged StableId keeps the existing
    // clone and only re-applies the grab, which is what stops device churn from recreating nodes.
    auto attach(libevdev* Device, std::string Name, std::string Identity)
        -> std::expected<void, Utils::Error>;
    auto detach() -> void;
    // Drops the borrowed handle without touching it, for when the owner is about to close the
    // descriptors it belongs to. The kernel releases the grab with them.
    auto invalidate() -> void;

    auto request(bool Engage, bool SwallowLeft, bool SwallowRight)
        -> std::expected<void, Utils::Error>;
    // Completes an engage that was deferred because the device was not idle.
    auto poll_pending() -> std::expected<void, Utils::Error>;

    [[nodiscard]] auto engaged() const noexcept -> bool;
    [[nodiscard]] auto swallows(std::uint16_t Type, std::uint16_t Code) const noexcept -> bool;
    auto forward(std::uint16_t Type, std::uint16_t Code, std::int32_t Value) -> void;
    // Releases every button the clone still holds, for recovery after a dropped event stream.
    auto release_all() -> void;
    [[nodiscard]] auto passthrough_node() const noexcept -> std::string_view;
    [[nodiscard]] auto passthrough_name() const noexcept -> std::string_view;

private:
    [[nodiscard]] auto device_idle() const -> bool;
    auto engage() -> std::expected<void, Utils::Error>;
    auto disengage() -> void;

    static constexpr auto KeyCount = std::size_t{768};

    libevdev* Device{};
    libevdev_uinput* Passthrough{};
    std::string DeviceName;
    std::string Identity;
    std::string PassthroughNode;
    std::string PassthroughName;
    std::array<bool, KeyCount> ForwardedDown{};
    bool Engaged{};
    bool Requested{};
    bool SwallowLeftButton{};
    bool SwallowRightButton{};
};

} // namespace Autoclicker::Input
