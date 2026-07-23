#include "TestHarness.hpp"

#include "Core/ClickScheduler.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <vector>

namespace Autoclicker::Tests {
namespace {
class FakeOutput final : public Core::ClickOutput {
public:
    auto emit(const Types::ClickTransition Transition) -> bool override {
        Transitions.push_back(Transition);
        return true;
    }

    std::vector<Types::ClickTransition> Transitions;
};
} // namespace

auto scheduler_tests() -> void {
    auto Output = FakeOutput{};
    auto Scheduler = Core::ClickScheduler{Output};
    auto Config = Types::ConfigurationData{};
    Config.Cps = 10;
    const auto Start = Core::ClickScheduler::TimePoint{};
    Scheduler.configure(Config, Start);
    Scheduler.set_enabled(true, Start);
    Scheduler.process(Start);
    require(Output.Transitions.size() == 1 && Output.Transitions.front() == Types::ClickTransition::Press,
            "normal mode must press on its first deadline");
    Scheduler.set_enabled(false, Start + std::chrono::milliseconds{1});
    require(Output.Transitions.back() == Types::ClickTransition::Release,
            "disable must force a pending release");
    require(!Scheduler.next_deadline(), "disable must cancel the timer");

    Output.Transitions.clear();
    Scheduler.set_enabled(true, Start);
    Scheduler.process(Start);
    Scheduler.set_output_allowed(false, Start);
    Scheduler.process(Start + std::chrono::seconds{1});
    require(Output.Transitions.size() == 2 && Output.Transitions.back() == Types::ClickTransition::Release,
            "focus loss must release a pending synthetic button and suppress further clicks");
    require(!Scheduler.next_deadline(), "focus loss must cancel the click timer");
    Scheduler.set_output_allowed(true, Start + std::chrono::seconds{2});
    require(Scheduler.next_deadline().has_value(), "restoring focus must resume an enabled scheduler");
    Scheduler.set_enabled(false, Start + std::chrono::seconds{2});

    Output.Transitions.clear();
    Config.Mode = Types::OperatingMode::Additive;
    Scheduler.configure(Config, Start);
    Scheduler.set_enabled(true, Start);
    Scheduler.process(Start);
    require(Output.Transitions.empty(), "additive mode must stay idle without a physical click");
    Scheduler.on_physical_button(true, Start);
    Scheduler.process(Start);
    require(Output.Transitions.empty(), "additive mode must not overlap a physical press");
    Scheduler.on_physical_button(true, Start + std::chrono::milliseconds{1});
    require(Scheduler.physical_cps(Start + std::chrono::milliseconds{1}) == 1.0,
            "duplicate BTN_LEFT presses from composite mouse nodes must count once");
    Scheduler.on_physical_button(false, Start + std::chrono::milliseconds{5});
    Scheduler.on_physical_button(false, Start + std::chrono::milliseconds{6});
    Scheduler.process(Start + std::chrono::milliseconds{99});
    require(Output.Transitions.empty(),
            "one isolated physical click must not produce a trailing synthetic hit");
    Scheduler.on_physical_button(true, Start + std::chrono::milliseconds{100});
    Scheduler.on_physical_button(false, Start + std::chrono::milliseconds{105});
    Scheduler.process(Start + std::chrono::milliseconds{200});
    require(!Output.Transitions.empty(), "additive mode must add clicks while physical clicking is active");

    auto MixedOutput = FakeOutput{};
    auto MixedScheduler = Core::ClickScheduler{MixedOutput};
    Config.Cps = 15;
    MixedScheduler.configure(Config, Start);
    MixedScheduler.set_enabled(true, Start);
    auto MinimumCombinedClicks = 1000;
    auto MaximumCombinedClicks = 0;
    for (auto Millisecond = 0; Millisecond < 30000; ++Millisecond) {
        const auto Now = Start + std::chrono::milliseconds{Millisecond};
        if (Millisecond % 100 == 0) MixedScheduler.on_physical_button(true, Now);
        if (Millisecond % 100 == 25) MixedScheduler.on_physical_button(false, Now);
        MixedScheduler.process(Now);
        if (Millisecond >= 5000 && Millisecond % 10 == 9) {
            const auto WindowClicks = static_cast<int>(
                MixedScheduler.physical_cps(Now) + MixedScheduler.emitted_cps(Now));
            MinimumCombinedClicks = std::min(MinimumCombinedClicks, WindowClicks);
            MaximumCombinedClicks = std::max(MaximumCombinedClicks, WindowClicks);
        }
    }
    const auto MixedEnd = Start + std::chrono::milliseconds{29999};
    const auto CombinedClicks = static_cast<int>(
        MixedScheduler.physical_cps(MixedEnd) + MixedScheduler.emitted_cps(MixedEnd));
    require(CombinedClicks >= 14 && CombinedClicks <= 16,
            std::string{"additive mode combined click count was "} + std::to_string(CombinedClicks) +
                ", tracked physical " + std::to_string(MixedScheduler.physical_cps(MixedEnd)) +
                ", tracked synthetic " + std::to_string(MixedScheduler.emitted_cps(MixedEnd)));
    require(MinimumCombinedClicks >= 14 && MaximumCombinedClicks <= 16,
            std::string{"30-second additive range was "} + std::to_string(MinimumCombinedClicks) +
                "-" + std::to_string(MaximumCombinedClicks) + " CPS");
    require(MixedOutput.Transitions.size() % 2 == 0,
            "additive output must contain complete press/release pairs");
    for (auto Index = std::size_t{}; Index < MixedOutput.Transitions.size(); Index += 2) {
        require(MixedOutput.Transitions[Index] == Types::ClickTransition::Press &&
                    MixedOutput.Transitions[Index + 1] == Types::ClickTransition::Release,
                "additive output must release each synthetic press immediately");
    }

    auto EightCpsOutput = FakeOutput{};
    auto EightCpsScheduler = Core::ClickScheduler{EightCpsOutput};
    EightCpsScheduler.configure(Config, Start);
    EightCpsScheduler.set_enabled(true, Start);
    auto EightCpsMinimumCombinedClicks = 1000;
    auto EightCpsMaximumCombinedClicks = 0;
    for (auto Millisecond = 0; Millisecond < 30000; ++Millisecond) {
        const auto Now = Start + std::chrono::milliseconds{Millisecond};
        if (Millisecond % 125 == 0) EightCpsScheduler.on_physical_button(true, Now);
        if (Millisecond % 125 == 25) EightCpsScheduler.on_physical_button(false, Now);
        EightCpsScheduler.process(Now);
        if (Millisecond >= 5000 && Millisecond % 10 == 9) {
            const auto WindowClicks = static_cast<int>(
                EightCpsScheduler.physical_cps(Now) + EightCpsScheduler.emitted_cps(Now));
            EightCpsMinimumCombinedClicks = std::min(EightCpsMinimumCombinedClicks, WindowClicks);
            EightCpsMaximumCombinedClicks = std::max(EightCpsMaximumCombinedClicks, WindowClicks);
        }
    }
    const auto EightCpsEnd = Start + std::chrono::milliseconds{29999};
    const auto EightCpsCombinedClicks = static_cast<int>(
        EightCpsScheduler.physical_cps(EightCpsEnd) + EightCpsScheduler.emitted_cps(EightCpsEnd));
    require(EightCpsCombinedClicks >= 14 && EightCpsCombinedClicks <= 16,
            std::string{"8 CPS additive combined click count was "} +
                std::to_string(EightCpsCombinedClicks));
    require(EightCpsMinimumCombinedClicks >= 14 && EightCpsMaximumCombinedClicks <= 16,
            std::string{"30-second 8 CPS additive range was "} +
                std::to_string(EightCpsMinimumCombinedClicks) + "-" +
                std::to_string(EightCpsMaximumCombinedClicks) + " CPS");

    auto HumanOutput = FakeOutput{};
    auto HumanScheduler = Core::ClickScheduler{HumanOutput};
    HumanScheduler.configure(Config, Start);
    HumanScheduler.set_enabled(true, Start);
    constexpr auto HumanIntervals = std::array{80, 110, 95, 130, 90, 105, 145, 85, 100, 120};
    auto NextHumanPress = 0;
    auto NextHumanRelease = -1;
    auto HumanIntervalIndex = std::size_t{};
    auto HumanMinimumCombinedClicks = 1000;
    auto HumanMaximumCombinedClicks = 0;
    for (auto Millisecond = 0; Millisecond < 30000; ++Millisecond) {
        const auto Now = Start + std::chrono::milliseconds{Millisecond};
        if (Millisecond == NextHumanPress) {
            HumanScheduler.on_physical_button(true, Now);
            NextHumanRelease = Millisecond + 25;
            NextHumanPress += HumanIntervals[HumanIntervalIndex];
            HumanIntervalIndex = (HumanIntervalIndex + 1) % HumanIntervals.size();
        }
        if (Millisecond == NextHumanRelease) HumanScheduler.on_physical_button(false, Now);
        HumanScheduler.process(Now);
        if (Millisecond >= 5000 && Millisecond % 10 == 9) {
            const auto WindowClicks = static_cast<int>(
                HumanScheduler.physical_cps(Now) + HumanScheduler.emitted_cps(Now));
            HumanMinimumCombinedClicks = std::min(HumanMinimumCombinedClicks, WindowClicks);
            HumanMaximumCombinedClicks = std::max(HumanMaximumCombinedClicks, WindowClicks);
        }
    }
    require(HumanMinimumCombinedClicks >= 14 && HumanMaximumCombinedClicks <= 16,
            std::string{"30-second uneven additive range was "} +
                std::to_string(HumanMinimumCombinedClicks) + "-" +
                std::to_string(HumanMaximumCombinedClicks) + " CPS");

    const auto PressesAtExpectedNextClick = std::ranges::count(
        EightCpsOutput.Transitions, Types::ClickTransition::Press);
    for (auto Millisecond = 30000; Millisecond < 30125; ++Millisecond) {
        EightCpsScheduler.process(Start + std::chrono::milliseconds{Millisecond});
    }
    const auto PressesAfterGrace = std::ranges::count(
        EightCpsOutput.Transitions, Types::ClickTransition::Press);
    require(PressesAfterGrace <= PressesAtExpectedNextClick + 1,
            "additive mode must never emit a trailing click burst while cadence expires");
    for (auto Millisecond = 30125; Millisecond < 30500; ++Millisecond) {
        EightCpsScheduler.process(Start + std::chrono::milliseconds{Millisecond});
    }
    require(std::ranges::count(EightCpsOutput.Transitions, Types::ClickTransition::Press) ==
                PressesAfterGrace,
            "additive mode must remain idle after physical cadence expires");

    Scheduler.set_enabled(false, Start + std::chrono::seconds{1});
    Output.Transitions.clear();
    Config.Mode = Types::OperatingMode::Normal;
    Config.Unit = Types::RateUnit::Cps;
    Config.Cps = 12;
    Config.RateOffset = 2;
    Scheduler.configure(Config, Start);
    Scheduler.set_enabled(true, Start);
    Scheduler.process(Start);
    const auto OffsetDeadline = Scheduler.next_deadline();
    require(OffsetDeadline.has_value(), "offset scheduling must produce a deadline");
    const auto HalfPeriod = *OffsetDeadline - Start;
    require(HalfPeriod >= std::chrono::nanoseconds{1'000'000'000LL / 14 / 2} &&
                HalfPeriod <= std::chrono::nanoseconds{1'000'000'000LL / 10 / 2},
            "a 12 CPS rate with offset 2 must remain in the 10-14 CPS range");

    Scheduler.set_enabled(false, Start + std::chrono::seconds{2});
    Output.Transitions.clear();
    Config.Unit = Types::RateUnit::Milliseconds;
    Config.Delay = std::chrono::milliseconds{8};
    Config.RateOffset = 0;
    Scheduler.configure(Config, Start);
    Scheduler.set_enabled(true, Start);
    Scheduler.process(Start);
    require(Scheduler.next_deadline() == Start + std::chrono::milliseconds{4},
            "millisecond mode must use half of its delay for each button transition");
}

} // namespace Autoclicker::Tests
