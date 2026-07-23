#pragma once

#include "Core/ClickScheduler.hpp"
#include "Types/IpcTypes.hpp"

namespace Autoclicker::Core {

class ShutdownCoordinator final {
public:
    explicit ShutdownCoordinator(ClickScheduler& Scheduler);

    auto set_enabled(bool Enabled, ClickScheduler::TimePoint Now) -> void;
    auto begin_shutdown() -> void;
    auto mark_stopped() noexcept -> void;
    [[nodiscard]] auto state() const noexcept -> Types::LifecycleState;
    [[nodiscard]] auto shutting_down() const noexcept -> bool;

private:
    ClickScheduler& SchedulerValue;
    Types::LifecycleState State{Types::LifecycleState::Disabled};
};

} // namespace Autoclicker::Core
