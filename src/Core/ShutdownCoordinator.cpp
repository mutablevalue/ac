#include "Core/ShutdownCoordinator.hpp"

namespace Autoclicker::Core {

ShutdownCoordinator::ShutdownCoordinator(ClickScheduler& Scheduler) : SchedulerValue(Scheduler) {}

auto ShutdownCoordinator::set_enabled(const bool Enabled, const ClickScheduler::TimePoint Now) -> void {
    if (shutting_down()) return;
    SchedulerValue.set_enabled(Enabled, Now);
    State = Enabled ? Types::LifecycleState::Enabled : Types::LifecycleState::Disabled;
}

auto ShutdownCoordinator::begin_shutdown() -> void {
    if (shutting_down()) return;
    State = Types::LifecycleState::ShuttingDown;
    SchedulerValue.set_enabled(false, ClickScheduler::Clock::now());
    SchedulerValue.force_release();
}

auto ShutdownCoordinator::mark_stopped() noexcept -> void { State = Types::LifecycleState::Stopped; }
auto ShutdownCoordinator::state() const noexcept -> Types::LifecycleState { return State; }
auto ShutdownCoordinator::shutting_down() const noexcept -> bool {
    return State == Types::LifecycleState::ShuttingDown || State == Types::LifecycleState::Stopped;
}

} // namespace Autoclicker::Core
