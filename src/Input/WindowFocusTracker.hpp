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
    [[nodiscard]] auto windows() const -> std::vector<Types::WindowCandidate>;
    [[nodiscard]] auto descriptor() const noexcept -> int;
    [[nodiscard]] auto focus_state(const std::optional<std::string>& TargetApplication,
                                   std::uint64_t OwnWindowId) const -> Types::WindowFocusState;

private:
    class Impl;
    std::unique_ptr<Impl> Implementation;
};

} // namespace Autoclicker::Input
