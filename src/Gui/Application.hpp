#pragma once

#include "Gui/DaemonClient.hpp"
#include "Input/WindowFocusTracker.hpp"
#include "Types/ConfigurationTypes.hpp"
#include "Types/GuiTypes.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace Autoclicker::Gui {

class Application final {
public:
    auto run() -> int;

private:
    auto initialize() -> bool;
    auto render() -> void;
    auto save_and_send() -> void;
    auto draw_numeric_control(const char* Label, int& Value, int Minimum, int Maximum,
                              Types::NumericEditTarget Target) -> bool;
    auto draw_binding(const char* Label, Types::KeyChord& Binding) -> bool;
    auto refresh_window_candidates() -> void;
    auto set_error(std::string Message) -> void;

    GLFWwindow* Window{};
    DaemonClient Daemon;
    Input::WindowFocusTracker FocusTracker;
    Types::ConfigurationData Config;
    std::vector<Types::WindowCandidate> WindowCandidates;
    bool Enabled{};
    bool Running{true};
    std::string ErrorMessage;
    std::chrono::steady_clock::time_point ErrorExpires{};
    std::chrono::steady_clock::time_point NextStatusRefresh{};
    std::chrono::steady_clock::time_point ConfigApplyDeadline{};
    bool ConfigDirty{};
    Types::NumericEditTarget NumericEditor{Types::NumericEditTarget::None};
    bool FocusNumericEditor{};
    std::uint64_t OwnWindowId{};
    std::uint64_t LastStatusRevision{};
    bool FocusTrackerReady{};
};

} // namespace Autoclicker::Gui
