#include "Core/ShutdownCoordinator.hpp"

namespace Autoclicker::Core {

ShutdownCoordinator::ShutdownCoordinator(ClickScheduler& LeftScheduler, ClickScheduler& RightScheduler)
    : Left(LeftScheduler), Right(RightScheduler) {}

auto ShutdownCoordinator::scheduler_for(const Types::MouseButton Button) const noexcept -> ClickScheduler& {
    return Button == Types::MouseButton::Right ? Right : Left;
}

auto ShutdownCoordinator::set_enabled(const Types::MouseButton Button, const bool Enabled,
                                      const ClickScheduler::TimePoint Now) -> void {
    if (shutting_down()) return;
    scheduler_for(Button).set_enabled(Enabled, Now);
    State = Left.enabled() || Right.enabled() ? Types::LifecycleState::Enabled
                                              : Types::LifecycleState::Disabled;
}

auto ShutdownCoordinator::begin_shutdown() -> void {
    if (shutting_down()) return;
    State = Types::LifecycleState::ShuttingDown;
    const auto Now = ClickScheduler::Clock::now();
    Left.set_enabled(false, Now);
    // quiesce also abandons an in-flight physical-multiply burst, which survives set_enabled.
    Left.quiesce();
    Right.set_enabled(false, Now);
    Right.quiesce();
}

auto ShutdownCoordinator::mark_stopped() noexcept -> void { State = Types::LifecycleState::Stopped; }

auto ShutdownCoordinator::enabled(const Types::MouseButton Button) const noexcept -> bool {
    return scheduler_for(Button).enabled();
}

auto ShutdownCoordinator::state() const noexcept -> Types::LifecycleState { return State; }
auto ShutdownCoordinator::shutting_down() const noexcept -> bool {
    return State == Types::LifecycleState::ShuttingDown || State == Types::LifecycleState::Stopped;
}

} // namespace Autoclicker::Core
