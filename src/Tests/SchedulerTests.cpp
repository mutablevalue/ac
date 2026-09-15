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
    auto emit(const Types::MouseButton Button, const Types::ClickTransition Transition) -> bool override {
        Buttons.push_back(Button);
        Transitions.push_back(Transition);
        Times.push_back(Now);
        return true;
    }

    auto clear() -> void {
        Buttons.clear();
        Transitions.clear();
        Times.clear();
    }

    [[nodiscard]] auto presses() const -> std::size_t {
        return static_cast<std::size_t>(std::ranges::count(Transitions, Types::ClickTransition::Press));
    }

    // Set by the test before each process() so emissions can be timestamped in virtual time.
    Core::ClickScheduler::TimePoint Now;
    std::vector<Types::MouseButton> Buttons;
    std::vector<Types::ClickTransition> Transitions;
    std::vector<Core::ClickScheduler::TimePoint> Times;
};

// Steps virtual time in one-millisecond ticks, keeping the sink's clock in sync.
auto run_for(Core::ClickScheduler& Scheduler, FakeOutput& Output,
             const Core::ClickScheduler::TimePoint Start, const std::chrono::milliseconds Duration)
    -> void {
    for (auto Elapsed = std::chrono::milliseconds{}; Elapsed <= Duration;
         Elapsed += std::chrono::milliseconds{1}) {
        Output.Now = Start + Elapsed;
        Scheduler.process(Start + Elapsed);
    }
}

auto alternates(const FakeOutput& Output) -> bool {
    for (auto Index = std::size_t{}; Index < Output.Transitions.size(); ++Index) {
        const auto Expected = Index % 2 == 0 ? Types::ClickTransition::Press
                                             : Types::ClickTransition::Release;
        if (Output.Transitions[Index] != Expected) return false;
    }
    return true;
}
} // namespace

auto scheduler_tests() -> void {
    auto Output = FakeOutput{};
    auto Scheduler = Core::ClickScheduler{Output, Types::MouseButton::Left};
    auto Config = Types::ChannelConfiguration{};
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
    auto MixedScheduler = Core::ClickScheduler{MixedOutput, Types::MouseButton::Left};
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
    auto EightCpsScheduler = Core::ClickScheduler{EightCpsOutput, Types::MouseButton::Left};
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
    auto HumanScheduler = Core::ClickScheduler{HumanOutput, Types::MouseButton::Right};
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
    require(!HumanOutput.Buttons.empty() &&
                std::ranges::all_of(HumanOutput.Buttons,
                                    [](const auto Value) { return Value == Types::MouseButton::Right; }),
            "a right-button scheduler must only emit right-button events");

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

    // The additive start rate must hold output back until physical clicking reaches it.
    auto GateConfig = Config;
    GateConfig.AdditiveStartCps = 8;

    auto SlowOutput = FakeOutput{};
    auto SlowScheduler = Core::ClickScheduler{SlowOutput, Types::MouseButton::Left};
    SlowScheduler.configure(GateConfig, Start);
    SlowScheduler.set_enabled(true, Start);
    for (auto Millisecond = 0; Millisecond < 10000; ++Millisecond) {
        const auto Now = Start + std::chrono::milliseconds{Millisecond};
        if (Millisecond % 200 == 0) SlowScheduler.on_physical_button(true, Now);
        if (Millisecond % 200 == 25) SlowScheduler.on_physical_button(false, Now);
        SlowScheduler.process(Now);
    }
    require(SlowOutput.Transitions.empty(),
            "clicking below the additive start rate must never generate a click");
    require(!SlowScheduler.additive_gate_open(), "a suppressed channel must report a closed gate");

    // The same cadence without a start rate must still fill in, proving the gate did the work.
    auto UngatedOutput = FakeOutput{};
    auto UngatedScheduler = Core::ClickScheduler{UngatedOutput, Types::MouseButton::Left};
    UngatedScheduler.configure(Config, Start);
    UngatedScheduler.set_enabled(true, Start);
    for (auto Millisecond = 0; Millisecond < 10000; ++Millisecond) {
        const auto Now = Start + std::chrono::milliseconds{Millisecond};
        if (Millisecond % 200 == 0) UngatedScheduler.on_physical_button(true, Now);
        if (Millisecond % 200 == 25) UngatedScheduler.on_physical_button(false, Now);
        UngatedScheduler.process(Now);
    }
    const auto UngatedEnd = Start + std::chrono::milliseconds{9999};
    const auto UngatedCombinedClicks = static_cast<int>(
        UngatedScheduler.physical_cps(UngatedEnd) + UngatedScheduler.emitted_cps(UngatedEnd));
    require(UngatedCombinedClicks >= 14 && UngatedCombinedClicks <= 16,
            std::string{"the same slow cadence without a start rate must reach the target, saw "} +
                std::to_string(UngatedCombinedClicks) + " CPS");

    auto GatedOutput = FakeOutput{};
    auto GatedScheduler = Core::ClickScheduler{GatedOutput, Types::MouseButton::Left};
    GatedScheduler.configure(GateConfig, Start);
    GatedScheduler.set_enabled(true, Start);
    auto GatedMaximumCombinedClicks = 0;
    auto GatedInterval = 200;
    auto NextGatedPress = 0;
    auto NextGatedRelease = -1;
    for (auto Millisecond = 0; Millisecond < 20000; ++Millisecond) {
        const auto Now = Start + std::chrono::milliseconds{Millisecond};
        // Click at 5 CPS for the first three seconds, then speed up to 10 CPS.
        if (Millisecond == 3000) GatedInterval = 100;
        if (Millisecond == NextGatedPress) {
            GatedScheduler.on_physical_button(true, Now);
            NextGatedRelease = Millisecond + 25;
            NextGatedPress = Millisecond + GatedInterval;
        }
        if (Millisecond == NextGatedRelease) GatedScheduler.on_physical_button(false, Now);
        GatedScheduler.process(Now);
        if (Millisecond == 2999) {
            require(GatedScheduler.emitted_cps(Now) == 0.0,
                    "the additive gate must stay shut while physical clicking is too slow");
        }
        if (Millisecond >= 3000) {
            GatedMaximumCombinedClicks = std::max(
                GatedMaximumCombinedClicks,
                static_cast<int>(GatedScheduler.physical_cps(Now) + GatedScheduler.emitted_cps(Now)));
        }
    }
    const auto GatedEnd = Start + std::chrono::milliseconds{19999};
    const auto GatedCombinedClicks = static_cast<int>(
        GatedScheduler.physical_cps(GatedEnd) + GatedScheduler.emitted_cps(GatedEnd));
    require(GatedScheduler.additive_gate_open(), "clicking past the start rate must open the gate");
    require(GatedCombinedClicks >= 14 && GatedCombinedClicks <= 16,
            std::string{"an opened additive gate must reach the target rate, saw "} +
                std::to_string(GatedCombinedClicks) + " CPS");
    require(GatedMaximumCombinedClicks <= 16,
            std::string{"crossing the additive start rate must not burst, peaked at "} +
                std::to_string(GatedMaximumCombinedClicks) + " CPS");

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

    // Hold mode: output exists only while the trigger button is physically down.
    Scheduler.set_enabled(false, Start + std::chrono::seconds{2});
    Output.clear();
    Config = Types::ChannelConfiguration{};
    Config.Mode = Types::OperatingMode::Hold;
    Config.Cps = 10;
    Scheduler.configure(Config, Start);
    Scheduler.set_enabled(true, Start);
    run_for(Scheduler, Output, Start, std::chrono::milliseconds{1000});
    require(Output.Transitions.empty(), "hold mode must stay silent while the button is up");
    require(!Scheduler.next_deadline(), "an idle hold channel must not arm the timer");

    Scheduler.on_physical_button(true, Start);
    require(Scheduler.next_deadline() == Start, "holding the button must schedule immediately");
    run_for(Scheduler, Output, Start, std::chrono::milliseconds{1000});
    require(alternates(Output), "hold output must be strictly alternating press/release pairs");
    const auto HoldRate = Scheduler.emitted_cps(Start + std::chrono::milliseconds{1000});
    require(HoldRate >= 9.0 && HoldRate <= 11.0,
            std::string{"a 10 CPS hold channel must emit near 10 CPS, saw "} +
                std::to_string(HoldRate));

    const auto HoldEnd = Start + std::chrono::milliseconds{1000};
    Scheduler.on_physical_button(false, HoldEnd);
    require(Output.Transitions.back() == Types::ClickTransition::Release,
            "releasing the trigger must release the synthetic button immediately");
    require(!Scheduler.next_deadline(), "releasing the trigger must cancel the hold timer");
    const auto HeldCount = Output.Transitions.size();
    run_for(Scheduler, Output, HoldEnd, std::chrono::milliseconds{500});
    require(Output.Transitions.size() == HeldCount, "hold mode must stop the moment the button is up");

    // Focus loss must release a hold stream exactly like it releases a normal one.
    Scheduler.on_physical_button(true, HoldEnd);
    run_for(Scheduler, Output, HoldEnd, std::chrono::milliseconds{20});
    Scheduler.set_output_allowed(false, HoldEnd + std::chrono::milliseconds{20});
    require(Output.Transitions.back() == Types::ClickTransition::Release,
            "focus loss must release a held hold-mode button");
    require(!Scheduler.next_deadline(), "focus loss must cancel the hold timer");
    Scheduler.set_output_allowed(true, HoldEnd + std::chrono::milliseconds{30});
    Scheduler.on_physical_button(false, HoldEnd + std::chrono::milliseconds{40});
    Scheduler.set_enabled(false, HoldEnd + std::chrono::milliseconds{50});

    // A burst emits ClickMultiplier complete pairs per scheduled slot.
    Output.clear();
    Config = Types::ChannelConfiguration{};
    Config.Cps = 5;
    Config.ClickMultiplier = 3;
    Config.MultiplierGap = std::chrono::milliseconds{40};
    Scheduler.configure(Config, Start);
    Scheduler.set_enabled(true, Start);
    run_for(Scheduler, Output, Start, std::chrono::milliseconds{2000});
    require(alternates(Output), "burst output must stay paired");
    const auto BurstPresses = Output.presses();
    require(BurstPresses >= 27 && BurstPresses <= 33,
            std::string{"5 CPS at 3 clicks per click must emit about 30 presses, saw "} +
                std::to_string(BurstPresses));

    // Pair P presses at index 2P and releases at 2P+1; every third pair closes a burst.
    auto SawIntraBurstGap = false;
    for (auto Pair = std::size_t{}; (2 * Pair) + 2 < Output.Times.size(); ++Pair) {
        if (Pair % 3 == 2) continue;
        const auto Delta = Output.Times[(2 * Pair) + 2] - Output.Times[(2 * Pair) + 1];
        require(Delta == std::chrono::milliseconds{40},
                std::string{"intra-burst spacing must equal the configured gap, saw "} +
                    std::to_string(
                        std::chrono::duration_cast<std::chrono::milliseconds>(Delta).count()) +
                    " ms");
        SawIntraBurstGap = true;
    }
    require(SawIntraBurstGap, "the burst test must observe at least one intra-burst gap");

    // Bursting must not disturb the slot cadence itself.
    auto SawSlotBoundary = false;
    for (auto Slot = std::size_t{}; ((Slot + 1) * 6) < Output.Times.size(); ++Slot) {
        const auto Delta = Output.Times[(Slot + 1) * 6] - Output.Times[Slot * 6];
        require(Delta == std::chrono::milliseconds{200},
                std::string{"a 5 CPS burst channel must keep a 200 ms slot period, saw "} +
                    std::to_string(
                        std::chrono::duration_cast<std::chrono::milliseconds>(Delta).count()) +
                    " ms");
        SawSlotBoundary = true;
    }
    require(SawSlotBoundary, "the burst test must observe at least one slot boundary");
    Scheduler.set_enabled(false, Start + std::chrono::seconds{3});

    // Physical multiplication runs with the channel switched off; that is the whole feature.
    Output.clear();
    Config = Types::ChannelConfiguration{};
    Config.ClickMultiplier = 2;
    Config.MultiplierGap = std::chrono::milliseconds{40};
    Config.MultiplyPhysicalClicks = true;
    Scheduler.configure(Config, Start);
    Scheduler.set_enabled(false, Start);
    Scheduler.set_output_allowed(true, Start);
    Scheduler.on_physical_button(true, Start);
    Scheduler.on_physical_button(false, Start + std::chrono::milliseconds{10});
    require(Scheduler.next_deadline() == Start + std::chrono::milliseconds{50},
            "a multiplied tap must arm its own timeline while the channel is disabled");
    run_for(Scheduler, Output, Start, std::chrono::milliseconds{300});
    require(Output.Transitions.size() == 2 && alternates(Output),
            "a doubled tap must add exactly one complete pair");
    require(Output.Times.front() == Start + std::chrono::milliseconds{50},
            "the added click must land one gap after the physical release");
    require(!Scheduler.next_deadline(), "a finished burst must clear its deadline");

    // A drag must never drop a stray click at the release position.
    Output.clear();
    Scheduler.on_physical_button(true, Start + std::chrono::seconds{1});
    Scheduler.on_physical_button(false, Start + std::chrono::seconds{1} + std::chrono::milliseconds{500});
    run_for(Scheduler, Output, Start + std::chrono::seconds{1}, std::chrono::milliseconds{800});
    require(Output.Transitions.empty(), "a held drag must not be multiplied");

    // Losing focus cancels an armed burst rather than firing it later.
    Output.clear();
    const auto Armed = Start + std::chrono::seconds{3};
    Scheduler.on_physical_button(true, Armed);
    Scheduler.on_physical_button(false, Armed + std::chrono::milliseconds{10});
    Scheduler.set_output_allowed(false, Armed + std::chrono::milliseconds{20});
    run_for(Scheduler, Output, Armed, std::chrono::milliseconds{300});
    require(Output.Transitions.empty(), "focus loss must cancel an armed physical burst");
    require(!Scheduler.next_deadline(), "focus loss must clear the physical burst deadline");
    Scheduler.set_output_allowed(true, Armed + std::chrono::milliseconds{400});

    // A fresh real press supersedes the previous tap's remaining clicks.
    Output.clear();
    const auto Rapid = Start + std::chrono::seconds{5};
    Config.ClickMultiplier = 4;
    Scheduler.configure(Config, Rapid);
    Scheduler.set_output_allowed(true, Rapid);
    Scheduler.on_physical_button(true, Rapid);
    Scheduler.on_physical_button(false, Rapid + std::chrono::milliseconds{10});
    run_for(Scheduler, Output, Rapid, std::chrono::milliseconds{60});
    Scheduler.on_physical_button(true, Rapid + std::chrono::milliseconds{61});
    const auto BeforeRepress = Output.Transitions.size();
    run_for(Scheduler, Output, Rapid + std::chrono::milliseconds{61}, std::chrono::milliseconds{300});
    require(Output.Transitions.size() == BeforeRepress,
            "a new physical press must abandon the previous tap's burst");
    Scheduler.on_physical_button(false, Rapid + std::chrono::milliseconds{400});

    // Hold mode captures its own button, so it must not also multiply physical clicks.
    Output.clear();
    Config = Types::ChannelConfiguration{};
    Config.Mode = Types::OperatingMode::Hold;
    Config.Cps = 10;
    Config.ClickMultiplier = 2;
    Config.MultiplyPhysicalClicks = true;
    Scheduler.configure(Config, Start);
    Scheduler.set_enabled(true, Start);
    Scheduler.on_physical_button(true, Start);
    Scheduler.on_physical_button(false, Start + std::chrono::milliseconds{10});
    Output.clear();
    run_for(Scheduler, Output, Start + std::chrono::milliseconds{10}, std::chrono::milliseconds{300});
    require(Output.Transitions.empty(),
            "an armed hold channel must not also multiply its own physical clicks");

    // quiesce must abandon both timelines at once.
    Scheduler.set_enabled(false, Start + std::chrono::seconds{1});
    Config.Mode = Types::OperatingMode::Normal;
    Scheduler.configure(Config, Start);
    Scheduler.set_output_allowed(true, Start);
    Scheduler.on_physical_button(true, Start);
    Scheduler.on_physical_button(false, Start + std::chrono::milliseconds{10});
    require(Scheduler.next_deadline().has_value(), "the burst must be armed before quiescing");
    Scheduler.quiesce();
    require(!Scheduler.next_deadline(), "quiesce must clear both timelines");

    // The configured CPS stays the combined target even when real clicks are being multiplied:
    // each added click consumes a slot, so the controller fills only what is still missing.
    auto DoubledOutput = FakeOutput{};
    auto DoubledScheduler = Core::ClickScheduler{DoubledOutput, Types::MouseButton::Left};
    auto DoubledConfig = Types::ChannelConfiguration{};
    DoubledConfig.Mode = Types::OperatingMode::Additive;
    DoubledConfig.Cps = 15;
    DoubledConfig.ClickMultiplier = 2;
    DoubledConfig.MultiplierGap = std::chrono::milliseconds{40};
    DoubledConfig.MultiplyPhysicalClicks = true;
    DoubledScheduler.configure(DoubledConfig, Start);
    DoubledScheduler.set_enabled(true, Start);
    auto DoubledMinimum = 1000;
    auto DoubledMaximum = 0;
    for (auto Millisecond = 0; Millisecond < 30000; ++Millisecond) {
        const auto Now = Start + std::chrono::milliseconds{Millisecond};
        if (Millisecond % 200 == 0) DoubledScheduler.on_physical_button(true, Now);
        if (Millisecond % 200 == 25) DoubledScheduler.on_physical_button(false, Now);
        DoubledOutput.Now = Now;
        DoubledScheduler.process(Now);
        if (Millisecond >= 5000 && Millisecond % 10 == 9) {
            const auto WindowClicks = static_cast<int>(
                DoubledScheduler.physical_cps(Now) + DoubledScheduler.emitted_cps(Now));
            DoubledMinimum = std::min(DoubledMinimum, WindowClicks);
            DoubledMaximum = std::max(DoubledMaximum, WindowClicks);
        }
    }
    require(DoubledMinimum >= 14 && DoubledMaximum <= 16,
            std::string{"additive combined rate with multiplication was "} +
                std::to_string(DoubledMinimum) + "-" + std::to_string(DoubledMaximum) + " CPS");
    require(DoubledOutput.Transitions.size() % 2 == 0 && alternates(DoubledOutput),
            "multiplied additive output must stay in complete press/release pairs");
}

} // namespace Autoclicker::Tests
