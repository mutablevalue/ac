#pragma once

#include "Types/WindowTypes.hpp"
#include "Utils/Error.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Autoclicker::Input {

class WindowFocusTracker final {
public:
    WindowFocusTracker();
    ~WindowFocusTracker();
    WindowFocusTracker(const WindowFocusTracker&) = delete;
    auto operator=(const WindowFocusTracker&) -> WindowFocusTracker& = delete;
    WindowFocusTracker(WindowFocusTracker&&) noexcept;
    auto operator=(WindowFocusTracker&&) noexcept -> WindowFocusTracker&;

    auto initialize() -> std::expected<void, Utils::Error>;
    auto drain_changes() -> bool;
    [[nodiscard]] auto windows() const -> std::vector<Types::ApplicationCandidate>;
    [[nodiscard]] auto descriptor() const noexcept -> int;
    // Reports what the compositor exposes about the focused window. Deciding whether that matches
    // the configured target needs the process inventory and therefore belongs to the caller.
    [[nodiscard]] auto focus_state(std::uint64_t OwnWindowId) const -> Types::WindowFocusState;

private:
    class Impl;
    std::unique_ptr<Impl> Implementation;
};

} // namespace Autoclicker::Input
