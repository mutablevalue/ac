#pragma once

#include "Gui/DaemonClient.hpp"
#include "Input/ApplicationInventory.hpp"
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
    auto draw_channel(const char* Label, Types::ChannelConfiguration& Channel,
                      Types::MouseButton Button, bool& EnabledState,
                      Types::NumericEditTarget RateTarget, Types::NumericEditTarget OffsetTarget,
                      Types::NumericEditTarget StartTarget, Types::NumericEditTarget MultiplierTarget,
                      Types::NumericEditTarget GapTarget, Types::BindingTarget Binding) -> bool;
    auto draw_channel_status(const char* Label, const Types::ChannelStatus& Channel,
                             const Types::ChannelConfiguration& Settings) -> void;
    auto draw_binding(const char* Label, Types::KeyChord& Binding, Types::BindingTarget Target) -> bool;
    auto apply_capture() -> bool;
    auto refresh_application_candidates() -> void;
    auto set_error(std::string Message) -> void;

    GLFWwindow* Window{};
    DaemonClient Daemon;
    Input::WindowFocusTracker FocusTracker;
    Input::ApplicationInventory Inventory;
    Types::ConfigurationData Config;
    std::vector<Types::ApplicationCandidate> ApplicationCandidates;
    bool LeftEnabled{};
    bool RightEnabled{};
    bool Running{true};
    std::string ErrorMessage;
    std::chrono::steady_clock::time_point ErrorExpires{};
    std::chrono::steady_clock::time_point NextStatusRefresh{};
    std::chrono::steady_clock::time_point ConfigApplyDeadline{};
    bool ConfigDirty{};
    Types::NumericEditTarget NumericEditor{Types::NumericEditTarget::None};
    bool FocusNumericEditor{};
    Types::BindingTarget CaptureTarget{Types::BindingTarget::None};
    std::uint32_t CaptureToken{};
    std::uint32_t NextCaptureToken{1};
    std::uint64_t OwnWindowId{};
    std::uint64_t LastStatusRevision{};
    bool FocusTrackerReady{};
};

} // namespace Autoclicker::Gui
