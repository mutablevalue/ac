#include "Core/ClickScheduler.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>

namespace Autoclicker::Core {

using Types::ChannelConfiguration;
using Types::ClickTransition;
using Types::MouseButton;
using Types::OperatingMode;
using Types::RateUnit;

namespace {
constexpr auto MinimumPhysicalInterval = std::chrono::milliseconds{20};
constexpr auto MaximumPhysicalInterval = std::chrono::milliseconds{500};
constexpr auto MaximumStreamInterval = std::chrono::milliseconds{300};
constexpr auto InitialActivityGrace = std::chrono::milliseconds{200};
constexpr auto MinimumActivityGrace = std::chrono::milliseconds{100};
constexpr auto MaximumActivityGrace = std::chrono::milliseconds{250};
// A press held longer than this is a drag, and multiplying it would drop a stray click at the
// release position.
constexpr auto MaximumTapDuration = std::chrono::milliseconds{250};
// Upper bound on how long a burst holds the button, so the pairs stay distinguishable clicks.
constexpr auto MaximumBurstPressWidth = std::chrono::milliseconds{20};
}

ClickScheduler::ClickScheduler(ClickOutput& Output, const MouseButton Button)
    : OutputSink(Output), TargetButton(Button),
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

auto ClickScheduler::configure(ChannelConfiguration Value, const TimePoint Now) -> void {
    quiesce();
    reset_additive_tracking();
    SyntheticHistory = {};
    Config = std::move(Value);
    if (Enabled && OutputAllowed && Config.Mode != OperatingMode::Hold) schedule_from(Now);
}

auto ClickScheduler::set_enabled(const bool IsEnabled, const TimePoint Now) -> void {
    if (Enabled == IsEnabled) return;
    Enabled = IsEnabled;
    if (!Enabled) {
        // The physical-multiply timeline deliberately survives: it is the one behaviour that
        // runs with the channel switched off.
        force_release();
        NextTransition.reset();
        SlotDeadline.reset();
        AutoBurstRemaining = 0;
        reset_additive_tracking();
        PhysicalHistory = {};
        SyntheticHistory = {};
        return;
    }
    if (OutputAllowed && Config.Mode != OperatingMode::Hold) schedule_from(Now);
}

auto ClickScheduler::set_output_allowed(const bool Allowed, const TimePoint Now) -> void {
    if (OutputAllowed == Allowed) return;
    OutputAllowed = Allowed;
    if (!OutputAllowed) {
        quiesce();
        reset_additive_tracking();
        return;
    }
    if (Enabled && Config.Mode != OperatingMode::Hold) schedule_from(Now);
}

auto ClickScheduler::on_physical_button(const bool Pressed, const TimePoint Now) -> void {
    if (PhysicalDown == Pressed) return;
    PhysicalDown = Pressed;
    if (Pressed) {
        // A real click replaces the current target slot; never queue a synthetic hit behind it.
        PhysicalPressedAt = Now;
        cancel_physical_burst();
        force_release();
        PhysicalHistory.push(Now);
        if (Config.Mode == OperatingMode::Hold) {
            if (Enabled && OutputAllowed) schedule_from(Now);
            return;
        }
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
    if (Config.Mode == OperatingMode::Hold) {
        // Releasing the trigger ends the stream immediately rather than at the next deadline.
        force_release();
        AutoBurstRemaining = 0;
        SlotDeadline.reset();
        NextTransition.reset();
    }
    arm_physical_burst(Now);
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

auto ClickScheduler::additive_rate_met() const -> bool {
    if (Config.AdditiveStartCps == 0) return true;
    if (!HasPhysicalInterval) return false;
    const auto Threshold = std::chrono::nanoseconds{1'000'000'000LL / Config.AdditiveStartCps};
    return SmoothedPhysicalInterval <= Threshold;
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
    if (!additive_rate_met()) {
        // Physical clicking is below the configured start rate. Park on the activity deadline and
        // hold credit at a single slot so crossing the threshold cannot release a burst.
        AdditiveCredit = std::min(AdditiveCredit, AdditivePeriod);
        NextTransition = *ActivityDeadline;
        return;
    }
    const auto Remaining = std::max(AdditivePeriod - AdditiveCredit, Clock::duration{});
    NextTransition = std::min(Now + Remaining, *ActivityDeadline);
}

auto ClickScheduler::schedule_from(const TimePoint Now) -> void {
    NextTransition = Now;
}

auto ClickScheduler::emit_press(const TimePoint Now) -> void {
    if (SyntheticDown) return;
    // Hold drives its stream while the trigger is physically down by construction, so it is the
    // one mode that must emit through a held physical button.
    if (PhysicalDown && Config.Mode != OperatingMode::Hold) return;
    if (OutputSink.emit(TargetButton, ClickTransition::Press)) {
        SyntheticDown = true;
        SyntheticHistory.push(Now);
    }
}

auto ClickScheduler::emit_release() -> void {
    if (!SyntheticDown) return;
    static_cast<void>(OutputSink.emit(TargetButton, ClickTransition::Release));
    SyntheticDown = false;
}

auto ClickScheduler::burst_press_width() const -> Clock::duration {
    const auto Cap = std::min(Clock::duration{Config.MultiplierGap / 2},
                              Clock::duration{MaximumBurstPressWidth});
    return std::max(std::min(CurrentHalfPeriod, Cap), Clock::duration{1});
}

auto ClickScheduler::physical_multiply_armed() const noexcept -> bool {
    // While a Hold channel is armed its button is captured, so there is no physical click
    // reaching applications for this timeline to multiply.
    return Config.MultiplyPhysicalClicks && Config.ClickMultiplier > 1 &&
           !(Config.Mode == OperatingMode::Hold && Enabled);
}

auto ClickScheduler::arm_physical_burst(const TimePoint Now) -> void {
    if (!physical_multiply_armed() || !OutputAllowed) return;
    if (!PhysicalPressedAt || Now - *PhysicalPressedAt > MaximumTapDuration) return;
    PhysicalBurstRemaining = static_cast<std::uint8_t>(Config.ClickMultiplier - 1);
    PhysicalBurstAt = Now + Config.MultiplierGap;
}

auto ClickScheduler::cancel_physical_burst() -> void {
    PhysicalBurstAt.reset();
    PhysicalBurstRemaining = 0;
}

auto ClickScheduler::pump_physical_burst(const TimePoint Now) -> void {
    if (!PhysicalBurstAt || Now < *PhysicalBurstAt) return;
    if (PhysicalDown) {
        // A new real press supersedes the previous tap's remaining clicks.
        cancel_physical_burst();
        return;
    }
    emit_press(Now);
    emit_release();
    // A multiplied click still consumes a target slot, so the additive controller keeps
    // treating the configured rate as the combined total.
    if (Config.Mode == OperatingMode::Additive && Enabled) consume_additive_slot(Now);
    if (PhysicalBurstRemaining > 0) --PhysicalBurstRemaining;
    if (PhysicalBurstRemaining > 0) {
        PhysicalBurstAt = Now + Config.MultiplierGap;
        return;
    }
    cancel_physical_burst();
}

auto ClickScheduler::process_normal(const TimePoint Now, const bool HoldMode) -> void {
    if (HoldMode && !PhysicalDown) {
        force_release();
        AutoBurstRemaining = 0;
        SlotDeadline.reset();
        NextTransition.reset();
        return;
    }
    if (!NextTransition) schedule_from(Now);
    if (Now < *NextTransition) return;

    if (SyntheticDown) {
        emit_release();
        if (AutoBurstRemaining > 0) {
            *NextTransition = Now + Config.MultiplierGap;
        } else if (Config.ClickMultiplier <= 1) {
            *NextTransition = Now + CurrentHalfPeriod;
        } else {
            // The burst is done, so the next slot starts on its own boundary. A burst that
            // overran its slot resumes immediately, which caps the rate instead of catching up.
            *NextTransition = std::max(Now, SlotDeadline.value_or(Now + CurrentHalfPeriod));
        }
        return;
    }

    if (!HoldMode && PhysicalDown) {
        // The physical click owns this target slot. Advance normally without a deferred release hit.
        *NextTransition = Now + period();
        return;
    }

    if (AutoBurstRemaining > 0) {
        --AutoBurstRemaining;
        emit_press(Now);
        *NextTransition = Now + (SyntheticDown ? burst_press_width() : Config.MultiplierGap);
        return;
    }

    const auto Period = period();
    CurrentHalfPeriod = std::max(Period / 2, Clock::duration{1});
    SlotDeadline = Now + Period;
    const auto Burst = Config.ClickMultiplier > 1;
    AutoBurstRemaining = Burst ? static_cast<std::uint8_t>(Config.ClickMultiplier - 1) : std::uint8_t{};
    const auto Width = Burst ? burst_press_width() : CurrentHalfPeriod;
    emit_press(Now);
    // The output sink refused the press; skip the whole slot rather than burst without it.
    if (!SyntheticDown) AutoBurstRemaining = 0;

    // Reschedule from the current time when late; deliberately never replay missed periods.
    *NextTransition = Now + (SyntheticDown ? Width : Period);
}

auto ClickScheduler::process(const TimePoint Now) -> void {
    const auto OneSecondAgo = Now - std::chrono::seconds{1};
    PhysicalHistory.prune(OneSecondAgo);
    SyntheticHistory.prune(OneSecondAgo);
    // Physical multiplication runs before the disabled early-out; it is independent of the
    // channel's own enable state.
    if (OutputAllowed) pump_physical_burst(Now);
    if (!Enabled || !OutputAllowed) return;
    if (Config.Mode != OperatingMode::Additive) {
        process_normal(Now, Config.Mode == OperatingMode::Hold);
        return;
    }
    process_additive(Now);
}

auto ClickScheduler::process_additive(const TimePoint Now) -> void {
    if (!additive_active(Now)) {
        force_release();
        NextTransition.reset();
        if (!PreviousPhysicalPress || Now - *PreviousPhysicalPress > MaximumStreamInterval) {
            reset_additive_tracking();
        }
        return;
    }
    if (!HasPhysicalInterval) {
        NextTransition.reset();
        return;
    }
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
    if (AdditiveCredit < AdditivePeriod || !additive_rate_met()) {
        schedule_additive(Now);
        return;
    }
    consume_additive_slot(Now);
    emit_press(Now);
    emit_release();
    schedule_additive(Now);
}

auto ClickScheduler::force_release() -> void { emit_release(); }

auto ClickScheduler::quiesce() -> void {
    force_release();
    NextTransition.reset();
    SlotDeadline.reset();
    AutoBurstRemaining = 0;
    cancel_physical_burst();
}

auto ClickScheduler::next_deadline() const -> std::optional<TimePoint> {
    if (!NextTransition) return PhysicalBurstAt;
    if (!PhysicalBurstAt) return NextTransition;
    return std::min(*NextTransition, *PhysicalBurstAt);
}
auto ClickScheduler::enabled() const noexcept -> bool { return Enabled; }
auto ClickScheduler::mode() const noexcept -> OperatingMode { return Config.Mode; }

auto ClickScheduler::additive_gate_open() const -> bool {
    return Config.Mode != OperatingMode::Additive || additive_rate_met();
}

auto ClickScheduler::physical_cps(const TimePoint Now) const -> double {
    PhysicalHistory.prune(Now - std::chrono::seconds{1});
    return static_cast<double>(PhysicalHistory.Size);
}

auto ClickScheduler::emitted_cps(const TimePoint Now) const -> double {
    SyntheticHistory.prune(Now - std::chrono::seconds{1});
    return static_cast<double>(SyntheticHistory.Size);
}

} // namespace Autoclicker::Core
