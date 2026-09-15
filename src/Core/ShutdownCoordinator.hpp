#pragma once

#include "Core/ClickScheduler.hpp"
#include "Types/IpcTypes.hpp"

namespace Autoclicker::Core {

class ShutdownCoordinator final {
public:
    ShutdownCoordinator(ClickScheduler& LeftScheduler, ClickScheduler& RightScheduler);

    auto set_enabled(Types::MouseButton Button, bool Enabled, ClickScheduler::TimePoint Now) -> void;
    auto begin_shutdown() -> void;
    auto mark_stopped() noexcept -> void;
    [[nodiscard]] auto enabled(Types::MouseButton Button) const noexcept -> bool;
    [[nodiscard]] auto state() const noexcept -> Types::LifecycleState;
    [[nodiscard]] auto shutting_down() const noexcept -> bool;

private:
    [[nodiscard]] auto scheduler_for(Types::MouseButton Button) const noexcept -> ClickScheduler&;

    ClickScheduler& Left;
    ClickScheduler& Right;
    Types::LifecycleState State{Types::LifecycleState::Disabled};
};

} // namespace Autoclicker::Core
