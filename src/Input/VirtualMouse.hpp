#pragma once

#include "Core/ClickScheduler.hpp"
#include "Utils/Error.hpp"

#include <expected>

struct libevdev_uinput;

namespace Autoclicker::Input {

class VirtualMouse final : public Core::ClickOutput {
public:
    VirtualMouse() = default;
    ~VirtualMouse() override;
    VirtualMouse(const VirtualMouse&) = delete;
    auto operator=(const VirtualMouse&) -> VirtualMouse& = delete;

    auto initialize() -> std::expected<void, Utils::Error>;
    auto emit(Types::ClickTransition Transition) -> bool override;
    [[nodiscard]] auto ready() const noexcept -> bool;

private:
    libevdev_uinput* Device{};
};

} // namespace Autoclicker::Input
