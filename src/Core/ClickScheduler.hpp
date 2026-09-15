#pragma once

#include "Types/ConfigurationTypes.hpp"
#include "Types/SchedulerTypes.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <random>

namespace Autoclicker::Core {

class ClickOutput {
public:
    virtual ~ClickOutput() = default;
    virtual auto emit(Types::MouseButton Button, Types::ClickTransition Transition) -> bool = 0;
};

class ClickScheduler final {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    ClickScheduler(ClickOutput& Output, Types::MouseButton Button);

    auto configure(Types::ChannelConfiguration Value, TimePoint Now) -> void;
    auto set_enabled(bool Enabled, TimePoint Now) -> void;
    auto set_output_allowed(bool Allowed, TimePoint Now) -> void;
    auto on_physical_button(bool Pressed, TimePoint Now) -> void;
    auto process(TimePoint Now) -> void;
    auto force_release() -> void;
    // Releases the button and abandons both the generated and the physical-multiply timelines.
    auto quiesce() -> void;

    [[nodiscard]] auto next_deadline() const -> std::optional<TimePoint>;
    [[nodiscard]] auto enabled() const noexcept -> bool;
    [[nodiscard]] auto mode() const noexcept -> Types::OperatingMode;
    [[nodiscard]] auto additive_gate_open() const -> bool;
    [[nodiscard]] auto physical_cps(TimePoint Now) const -> double;
    [[nodiscard]] auto emitted_cps(TimePoint Now) const -> double;

private:
    struct TimestampRing final {
        static constexpr auto Capacity = std::size_t{2048};
        std::array<TimePoint, Capacity> Values{};
        std::size_t Begin{};
        std::size_t Size{};

        auto push(TimePoint Value) -> void;
        auto prune(TimePoint Minimum) -> void;
    };

    [[nodiscard]] auto period() -> Clock::duration;
    auto process_normal(TimePoint Now, bool HoldMode) -> void;
    auto process_additive(TimePoint Now) -> void;
    [[nodiscard]] auto burst_press_width() const -> Clock::duration;
    [[nodiscard]] auto physical_multiply_armed() const noexcept -> bool;
    auto arm_physical_burst(TimePoint Now) -> void;
    auto pump_physical_burst(TimePoint Now) -> void;
    auto cancel_physical_burst() -> void;
    [[nodiscard]] auto additive_active(TimePoint Now) const -> bool;
    [[nodiscard]] auto additive_rate_met() const -> bool;
    auto reset_additive_tracking() -> void;
    auto initialize_additive_controller(TimePoint Now) -> void;
    auto update_additive_credit(TimePoint Now) -> void;
    auto consume_additive_slot(TimePoint Now) -> void;
    auto schedule_additive(TimePoint Now) -> void;
    auto schedule_from(TimePoint Now) -> void;
    auto emit_press(TimePoint Now) -> void;
    auto emit_release() -> void;

    ClickOutput& OutputSink;
    Types::MouseButton TargetButton;
    Types::ChannelConfiguration Config;
    bool Enabled{};
    bool OutputAllowed{true};
    bool PhysicalDown{};
    bool SyntheticDown{};
    bool HasPhysicalInterval{};
    std::optional<TimePoint> PreviousPhysicalPress;
    std::optional<TimePoint> ActivityDeadline;
    std::optional<TimePoint> CreditUpdatedAt;
    std::optional<TimePoint> NextTransition;
    // Physical multiplication runs on its own timeline because it must survive the disabled
    // early-out in process().
    std::optional<TimePoint> PhysicalBurstAt;
    std::optional<TimePoint> PhysicalPressedAt;
    std::optional<TimePoint> SlotDeadline;
    std::uint8_t AutoBurstRemaining{};
    std::uint8_t PhysicalBurstRemaining{};
    Clock::duration SmoothedPhysicalInterval{std::chrono::milliseconds{125}};
    Clock::duration AdditiveCredit{};
    Clock::duration AdditivePeriod{std::chrono::milliseconds{100}};
    Clock::duration CurrentHalfPeriod{Clock::duration{1}};
    mutable TimestampRing PhysicalHistory;
    mutable TimestampRing SyntheticHistory;
    std::minstd_rand RateGenerator;
};

} // namespace Autoclicker::Core
