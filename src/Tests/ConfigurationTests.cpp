#include "TestHarness.hpp"

#include "Core/Configuration.hpp"

#include <filesystem>
#include <fstream>
#include <linux/input-event-codes.h>
#include <unistd.h>

namespace Autoclicker::Tests {

auto configuration_tests() -> void {
    auto Value = Types::ConfigurationData{};
    require(Core::Configuration::validate(Value).has_value(), "default configuration must be valid");
    Value.Left.Cps = 0;
    require(!Core::Configuration::validate(Value), "zero CPS must be rejected");
    Value.Left.Cps = 1000;
    Value.Left.Delay = std::chrono::milliseconds{10000};
    Value.Left.RateOffset = 10000;
    Value.Left.AdditiveStartCps = 1000;
    require(Core::Configuration::validate(Value).has_value(), "upper rate boundaries must be valid");
    Value.Left.RateOffset = 10001;
    require(!Core::Configuration::validate(Value), "an excessive rate offset must be rejected");
    Value.Left.RateOffset = 0;
    Value.Right.Cps = 0;
    require(!Core::Configuration::validate(Value), "the right channel must be validated too");
    Value.Right.Cps = 20;
    Value.Right.ToggleBinding = Value.Left.ToggleBinding;
    require(!Core::Configuration::validate(Value), "two channels sharing a toggle must be rejected");
    Value.Right.ToggleBinding = {KEY_F9, false, false, false, false};
    Value.ExitBinding = Value.Left.ToggleBinding;
    require(!Core::Configuration::validate(Value), "duplicate bindings must be rejected");
    Value.ExitBinding = {KEY_LEFTCTRL, false, false, false, false};
    require(!Core::Configuration::validate(Value), "modifier-only bindings must be rejected");

    Value = {};
    Value.Left.ClickMultiplier = 0;
    require(!Core::Configuration::validate(Value), "a zero click multiplier must be rejected");
    Value.Left.ClickMultiplier = 11;
    require(!Core::Configuration::validate(Value), "an excessive click multiplier must be rejected");
    Value.Left.ClickMultiplier = 10;
    require(Core::Configuration::validate(Value).has_value(), "ten clicks per click must be valid");
    Value.Left.MultiplierGap = std::chrono::milliseconds{4};
    require(!Core::Configuration::validate(Value), "a multiplier gap below 5 ms must be rejected");
    Value.Left.MultiplierGap = std::chrono::milliseconds{151};
    require(!Core::Configuration::validate(Value), "a multiplier gap above 150 ms must be rejected");
    Value.Left.MultiplierGap = std::chrono::milliseconds{150};
    require(Core::Configuration::validate(Value).has_value(), "a 150 ms multiplier gap must be valid");

    // Hold captures its own button, so binding the toggle to it would be unusable.
    Value = {};
    Value.Left.Mode = Types::OperatingMode::Hold;
    Value.Left.ToggleBinding = {BTN_LEFT, false, false, false, false};
    require(!Core::Configuration::validate(Value),
            "hold mode must reject a toggle bound to the button it captures");
    Value.Left.ToggleBinding = {BTN_RIGHT, false, false, false, false};
    require(Core::Configuration::validate(Value).has_value(),
            "hold mode may be toggled by a different mouse button");
    Value = {};
    Value.Left.ToggleBinding = {BTN_SIDE, false, false, false, false};
    require(Core::Configuration::validate(Value).has_value(), "mouse buttons must be bindable");

    const auto TestDirectory = std::filesystem::temp_directory_path() /
        ("fastclicker-tests-" + std::to_string(getpid()));
    const auto ConfigPath = TestDirectory / "config.json";
    Value = {};
    Value.Left.Cps = 42;
    Value.Left.RateOffset = 3;
    Value.Left.Mode = Types::OperatingMode::Additive;
    Value.Left.AdditiveStartCps = 6;
    Value.Right.Cps = 9;
    Value.Right.Delay = std::chrono::milliseconds{77};
    Value.Right.Mode = Types::OperatingMode::Hold;
    Value.Right.ClickMultiplier = 3;
    Value.Right.MultiplierGap = std::chrono::milliseconds{35};
    Value.Right.MultiplyPhysicalClicks = true;
    Value.TargetApplication = "Minecraft";
    auto& Configuration = Core::Configuration::instance();
    require(Configuration.update(Value).has_value(), "valid configuration update must succeed");
    require(Configuration.save(ConfigPath).has_value(), "configuration save must succeed");
    Value.Left.Cps = 7;
    require(Configuration.update(Value).has_value(), "second configuration update must succeed");
    require(Configuration.load(ConfigPath).has_value(), "saved configuration must load");
    require(Configuration.snapshot().Left.Cps == 42, "configuration must survive a save/load round trip");
    require(Configuration.snapshot().Left.RateOffset == 3, "rate offset must survive a save/load round trip");
    require(Configuration.snapshot().Left.AdditiveStartCps == 6,
            "additive start rate must survive a save/load round trip");
    require(Configuration.snapshot().Right.Cps == 9 &&
                Configuration.snapshot().Right.Delay == std::chrono::milliseconds{77},
            "the right channel must survive a save/load round trip");
    require(Configuration.snapshot().TargetApplication == "Minecraft",
            "target application must survive a save/load round trip");
    require(Configuration.snapshot().Right.Mode == Types::OperatingMode::Hold,
            "hold mode must survive a save/load round trip");
    require(Configuration.snapshot().Right.ClickMultiplier == 3 &&
                Configuration.snapshot().Right.MultiplierGap == std::chrono::milliseconds{35} &&
                Configuration.snapshot().Right.MultiplyPhysicalClicks,
            "the multiplier settings must survive a save/load round trip");

    // Configurations written before per-button channels must migrate into the left channel.
    {
        auto Stream = std::ofstream{ConfigPath, std::ios::trunc};
        Stream << R"({"mode":"additive","rate_unit":"cps","cps":22,"delay_ms":50,"rate_offset":2,)"
                  R"("target_application":"Minecraft 1.8.9",)"
                  R"("toggle_binding":{"key_code":110,"control":false,"alt":false,"shift":false,"super":false},)"
                  R"("exit_binding":{"key_code":88,"control":true,"alt":false,"shift":true,"super":false}})"
               << '\n';
    }
    require(Configuration.load(ConfigPath).has_value(), "a legacy flat configuration must load");
    const auto Migrated = Configuration.snapshot();
    require(Migrated.Left.Mode == Types::OperatingMode::Additive && Migrated.Left.Cps == 22 &&
                Migrated.Left.RateOffset == 2 && Migrated.Left.ToggleBinding.KeyCode == 110,
            "legacy settings must migrate into the left channel");
    require(Migrated.Left.AdditiveStartCps == 0, "a migrated channel must default to no additive gate");
    // Documents written before the multiplier existed must load with it switched off.
    require(Migrated.Left.ClickMultiplier == 1 &&
                Migrated.Left.MultiplierGap == std::chrono::milliseconds{40} &&
                !Migrated.Left.MultiplyPhysicalClicks,
            "a configuration without multiplier keys must load the inert defaults");
    require(Migrated.Right.Cps == 20 && Migrated.Right.Mode == Types::OperatingMode::Normal,
            "a migrated configuration must leave the right channel at its defaults");
    require(Migrated.Right.ToggleBinding != Migrated.Left.ToggleBinding,
            "migration must not leave the two channels sharing a toggle binding");
    require(Migrated.TargetApplication == "Minecraft 1.8.9",
            "a legacy target application must survive migration");

    // A legacy toggle that collides with the new right-channel default must still load.
    {
        auto Stream = std::ofstream{ConfigPath, std::ios::trunc};
        Stream << R"({"cps":15,"toggle_binding":{"key_code":67,"control":false,"alt":false,)"
                  R"("shift":false,"super":false}})" << '\n';
    }
    require(Configuration.load(ConfigPath).has_value(),
            "a legacy toggle colliding with the right default must still load");
    require(Configuration.snapshot().Right.ToggleBinding.KeyCode != 67,
            "a colliding right-channel default must be moved aside");

    {
        auto Stream = std::ofstream{ConfigPath, std::ios::trunc};
        Stream << "not json\n";
    }
    require(!Configuration.load(ConfigPath), "corrupt configuration must be rejected");
    require(Configuration.snapshot().Left.Cps == 15,
            "failed load must preserve the last valid configuration");
    std::filesystem::remove_all(TestDirectory);
}

} // namespace Autoclicker::Tests
