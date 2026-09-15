#pragma once

#include "Types/InputTypes.hpp"
#include "Utils/Error.hpp"

#include <expected>
#include <functional>
#include <memory>

namespace Autoclicker::Input {

class InputListener final {
public:
    using EventCallback = std::function<void(const Types::RawInputEvent&)>;

    InputListener();
    ~InputListener();
    InputListener(const InputListener&) = delete;
    auto operator=(const InputListener&) -> InputListener& = delete;
    InputListener(InputListener&&) noexcept;
    auto operator=(InputListener&&) noexcept -> InputListener&;

    auto start(EventCallback Callback) -> std::expected<void, Utils::Error>;
    auto drain() -> std::expected<void, Utils::Error>;
    // Captures the elected pointer so a held button can drive a click stream. The listener owns
    // the descriptor and the libevdev handle the grab applies to, so the grab can never outlive
    // them and callers never see a platform type.
    auto set_grab(bool Engage, bool SwallowLeft, bool SwallowRight)
        -> std::expected<void, Utils::Error>;
    [[nodiscard]] auto descriptor() const noexcept -> int;
    [[nodiscard]] auto mouse_connected() const noexcept -> bool;
    [[nodiscard]] auto grab_engaged() const noexcept -> bool;

private:
    class Impl;
    std::unique_ptr<Impl> Implementation;
};

} // namespace Autoclicker::Input
