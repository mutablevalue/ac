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
    Value.Cps = 0;
    require(!Core::Configuration::validate(Value), "zero CPS must be rejected");
    Value.Cps = 1000;
    Value.Delay = std::chrono::milliseconds{10000};
    Value.RateOffset = 10000;
    require(Core::Configuration::validate(Value).has_value(), "upper rate boundaries must be valid");
    Value.RateOffset = 10001;
    require(!Core::Configuration::validate(Value), "an excessive rate offset must be rejected");
    Value.RateOffset = 0;
    Value.ExitBinding = Value.ToggleBinding;
    require(!Core::Configuration::validate(Value), "duplicate bindings must be rejected");
    Value.ExitBinding = {KEY_LEFTCTRL, false, false, false, false};
    require(!Core::Configuration::validate(Value), "modifier-only bindings must be rejected");

    const auto TestDirectory = std::filesystem::temp_directory_path() /
        ("fastclicker-tests-" + std::to_string(getpid()));
    const auto ConfigPath = TestDirectory / "config.json";
    Value = {};
    Value.Cps = 42;
    Value.RateOffset = 3;
    Value.TargetApplication = "Minecraft";
    auto& Configuration = Core::Configuration::instance();
    require(Configuration.update(Value).has_value(), "valid configuration update must succeed");
    require(Configuration.save(ConfigPath).has_value(), "configuration save must succeed");
    Value.Cps = 7;
    require(Configuration.update(Value).has_value(), "second configuration update must succeed");
    require(Configuration.load(ConfigPath).has_value(), "saved configuration must load");
    require(Configuration.snapshot().Cps == 42, "configuration must survive a save/load round trip");
    require(Configuration.snapshot().RateOffset == 3, "rate offset must survive a save/load round trip");
    require(Configuration.snapshot().TargetApplication == "Minecraft",
            "target application must survive a save/load round trip");
    {
        auto Stream = std::ofstream{ConfigPath, std::ios::trunc};
        Stream << "not json\n";
    }
    require(!Configuration.load(ConfigPath), "corrupt configuration must be rejected");
    require(Configuration.snapshot().Cps == 42, "failed load must preserve the last valid configuration");
    std::filesystem::remove_all(TestDirectory);
}

} // namespace Autoclicker::Tests
