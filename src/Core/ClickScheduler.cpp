#include "Core/ClickScheduler.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>

namespace Autoclicker::Core {

using Types::ClickTransition;
using Types::ConfigurationData;
using Types::OperatingMode;
using Types::RateUnit;

namespace {
constexpr auto MinimumPhysicalInterval = std::chrono::milliseconds{20};
constexpr auto MaximumPhysicalInterval = std::chrono::milliseconds{500};
constexpr auto MaximumStreamInterval = std::chrono::milliseconds{300};
constexpr auto InitialActivityGrace = std::chrono::milliseconds{200};
constexpr auto MinimumActivityGrace = std::chrono::milliseconds{100};
constexpr auto MaximumActivityGrace = std::chrono::milliseconds{250};
}

ClickScheduler::ClickScheduler(ClickOutput& Output)
    : OutputSink(Output),
      RateGenerator(static_cast<std::minstd_rand::result_type>(Clock::now().time_since_epoch().count())) {}

auto ClickScheduler::TimestampRing::push(const TimePoint Value) -> void {
    if (Size < Capacity) {
        Values[(Begin + Size) % Capacity] = Value;
        ++Size;
        return;
    }
    Values[Begin] = Value;
    Begin = (Begin + 1) % Capacity;
}

auto ClickScheduler::TimestampRing::prune(const TimePoint Minimum) -> void {
    while (Size > 0 && Values[Begin] <= Minimum) {
        Begin = (Begin + 1) % Capacity;
        --Size;
    }
}

auto ClickScheduler::configure(ConfigurationData Value, const TimePoint Now) -> void {
    force_release();
    reset_additive_tracking();
    SyntheticHistory = {};
    Config = std::move(Value);
    if (Enabled && OutputAllowed) schedule_from(Now);
}

auto ClickScheduler::set_enabled(const bool IsEnabled, const TimePoint Now) -> void {
    if (Enabled == IsEnabled) return;
    Enabled = IsEnabled;
    if (!Enabled) {
        force_release();
        NextTransition.reset();
        reset_additive_tracking();
        PhysicalHistory = {};
        SyntheticHistory = {};
        return;
    }
    if (OutputAllowed) schedule_from(Now);
}

auto ClickScheduler::set_output_allowed(const bool Allowed, const TimePoint Now) -> void {
    if (OutputAllowed == Allowed) return;
    OutputAllowed = Allowed;
    if (!OutputAllowed) {
        force_release();
        NextTransition.reset();
        reset_additive_tracking();
        return;
    }
    if (Enabled) schedule_from(Now);
}

auto ClickScheduler::on_physical_button(const bool Pressed, const TimePoint Now) -> void {
    if (PhysicalDown == Pressed) return;
    PhysicalDown = Pressed;
    if (Pressed) {
        // A real click replaces the current target slot; never queue a synthetic hit behind it.
        force_release();
        PhysicalHistory.push(Now);
        if (Enabled && OutputAllowed && Config.Mode == OperatingMode::Additive) {
            if (ActivityDeadline && Now > *ActivityDeadline && PreviousPhysicalPress &&
                Now - *PreviousPhysicalPress > MaximumStreamInterval) {
                reset_additive_tracking();
            }
            const auto HadPhysicalInterval = HasPhysicalInterval;
            if (PreviousPhysicalPress) {
                const auto Interval = Now - *PreviousPhysicalPress;
                if (Interval >= MinimumPhysicalInterval && Interval <= MaximumPhysicalInterval) {
                    SmoothedPhysicalInterval = HasPhysicalInterval
                        ? (SmoothedPhysicalInterval * 3 + Interval) / 4
                        : Interval;
                    HasPhysicalInterval = true;
                }
            }
            PreviousPhysicalPress = Now;
            const auto ActivityGrace = HasPhysicalInterval
                ? std::clamp(SmoothedPhysicalInterval + SmoothedPhysicalInterval / 2,
                             Clock::duration{MinimumActivityGrace}, Clock::duration{MaximumActivityGrace})
                : Clock::duration{InitialActivityGrace};
            ActivityDeadline = Now + ActivityGrace;
            if (HasPhysicalInterval) {
                if (HadPhysicalInterval) {
                    consume_additive_slot(Now);
                } else {
                    initialize_additive_controller(Now);
                }
                schedule_additive(Now);
            } else {
                NextTransition.reset();
            }
        }
        return;
    }
    if (Enabled && OutputAllowed && Config.Mode == OperatingMode::Additive && HasPhysicalInterval) {
        update_additive_credit(Now);
        if (AdditiveCredit >= AdditivePeriod) {
            // A long physical hold owns the overdue slot; do not fire a click on release.
            AdditiveCredit = {};
            CreditUpdatedAt = Now;
        }
        schedule_additive(Now);
    }
}

auto ClickScheduler::period() -> Clock::duration {
    const auto Offset = static_cast<int>(Config.RateOffset);
    if (Config.Unit == RateUnit::Milliseconds) {
        const auto Base = static_cast<int>(Config.Delay.count());
        const auto Minimum = std::max(1, Base - Offset);
        const auto Maximum = std::min(10000, Base + Offset);
        return std::chrono::milliseconds{std::uniform_int_distribution<int>{Minimum, Maximum}(RateGenerator)};
    }
    const auto Base = static_cast<int>(Config.Cps);
    const auto Minimum = std::max(1, Base - Offset);
    const auto Maximum = std::min(1000, Base + Offset);
    const auto SampledCps = std::uniform_int_distribution<int>{Minimum, Maximum}(RateGenerator);
    return std::chrono::nanoseconds{1'000'000'000LL / SampledCps};
}

auto ClickScheduler::additive_active(const TimePoint Now) const -> bool {
    return ActivityDeadline && Now < *ActivityDeadline;
}

auto ClickScheduler::reset_additive_tracking() -> void {
    HasPhysicalInterval = false;
    PreviousPhysicalPress.reset();
    ActivityDeadline.reset();
    CreditUpdatedAt.reset();
    AdditiveCredit = {};
}

auto ClickScheduler::initialize_additive_controller(const TimePoint Now) -> void {
    AdditiveCredit = {};
    AdditivePeriod = period();
    CreditUpdatedAt = Now;
}

auto ClickScheduler::update_additive_credit(const TimePoint Now) -> void {
    if (!CreditUpdatedAt) {
        CreditUpdatedAt = Now;
        return;
    }
    if (Now <= *CreditUpdatedAt) return;
    const auto MaximumCredit = std::max(AdditivePeriod * 2, Clock::duration{1});
    AdditiveCredit = std::min(AdditiveCredit + (Now - *CreditUpdatedAt), MaximumCredit);
    CreditUpdatedAt = Now;
}

auto ClickScheduler::consume_additive_slot(const TimePoint Now) -> void {
    update_additive_credit(Now);
    // One real or generated click consumes one target slot. Discard excess credit so
    // scheduler delays can never turn into a catch-up burst.
    AdditiveCredit = std::min(AdditiveCredit, AdditivePeriod) - AdditivePeriod;
    AdditivePeriod = period();
    AdditiveCredit = std::clamp(AdditiveCredit, -AdditivePeriod, AdditivePeriod);
}

auto ClickScheduler::schedule_additive(const TimePoint Now) -> void {
    if (!HasPhysicalInterval || !ActivityDeadline || !CreditUpdatedAt) {
        NextTransition.reset();
        return;
    }
    update_additive_credit(Now);
    const auto Remaining = std::max(AdditivePeriod - AdditiveCredit, Clock::duration{});
    NextTransition = std::min(Now + Remaining, *ActivityDeadline);
}

auto ClickScheduler::schedule_from(const TimePoint Now) -> void {
    NextTransition = Now;
}

auto ClickScheduler::emit_press(const TimePoint Now) -> void {
    if (PhysicalDown || SyntheticDown) return;
    if (OutputSink.emit(ClickTransition::Press)) {
        SyntheticDown = true;
        SyntheticHistory.push(Now);
    }
}

auto ClickScheduler::emit_release() -> void {
    if (!SyntheticDown) return;
    static_cast<void>(OutputSink.emit(ClickTransition::Release));
    SyntheticDown = false;
}

auto ClickScheduler::process(const TimePoint Now) -> void {
    const auto OneSecondAgo = Now - std::chrono::seconds{1};
    PhysicalHistory.prune(OneSecondAgo);
    SyntheticHistory.prune(OneSecondAgo);
    if (!Enabled || !OutputAllowed) return;

    if (Config.Mode == OperatingMode::Additive && !additive_active(Now)) {
        force_release();
        NextTransition.reset();
        if (!PreviousPhysicalPress || Now - *PreviousPhysicalPress > MaximumStreamInterval) {
            reset_additive_tracking();
        }
        return;
    }
    if (Config.Mode == OperatingMode::Additive && !HasPhysicalInterval) {
        NextTransition.reset();
        return;
    }
    if (Config.Mode == OperatingMode::Additive) {
        if (!NextTransition) schedule_additive(Now);
        if (!NextTransition || Now < *NextTransition) return;

        if (SyntheticDown) {
            emit_release();
            schedule_additive(Now);
            return;
        }
        if (PhysicalDown) {
            NextTransition = ActivityDeadline;
            return;
        }

        update_additive_credit(Now);
        if (AdditiveCredit < AdditivePeriod) {
            schedule_additive(Now);
            return;
        }
        consume_additive_slot(Now);
        emit_press(Now);
        emit_release();
        schedule_additive(Now);
        return;
    }
    if (!NextTransition) schedule_from(Now);
    if (Now < *NextTransition) return;

    if (SyntheticDown) {
        emit_release();
        *NextTransition = Now + CurrentHalfPeriod;
        return;
    }

    if (PhysicalDown) {
        // The physical click owns this target slot. Advance normally without a deferred release hit.
        *NextTransition = Now + period();
        return;
    }

    const auto Period = period();
    CurrentHalfPeriod = std::max(Period / 2, Clock::duration{1});
    emit_press(Now);

    // Reschedule from the current time when late; deliberately never replay missed periods.
    *NextTransition = Now + (SyntheticDown ? CurrentHalfPeriod : Period);
}

auto ClickScheduler::force_release() -> void { emit_release(); }

auto ClickScheduler::next_deadline() const -> std::optional<TimePoint> { return NextTransition; }
auto ClickScheduler::enabled() const noexcept -> bool { return Enabled; }

auto ClickScheduler::physical_cps(const TimePoint Now) const -> double {
    PhysicalHistory.prune(Now - std::chrono::seconds{1});
    return static_cast<double>(PhysicalHistory.Size);
}

auto ClickScheduler::emitted_cps(const TimePoint Now) const -> double {
    SyntheticHistory.prune(Now - std::chrono::seconds{1});
    return static_cast<double>(SyntheticHistory.Size);
}

} // namespace Autoclicker::Core
