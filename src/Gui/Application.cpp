#include "Gui/Application.hpp"

#include "Core/Configuration.hpp"
#include "Utils/KeyNames.hpp"
#include "Utils/Logger.hpp"
#include "Utils/Paths.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#undef None
#undef Status
#include <linux/input-event-codes.h>
#include <utility>

namespace Autoclicker::Gui {
namespace {
auto glfw_error(const int, const char* Description) -> void {
    Utils::Logger::instance().log(
        Types::LogLevel::Error, Description == nullptr ? "GLFW error" : Description);
}

constexpr auto WarningColour = ImVec4{1.0F, 0.55F, 0.35F, 1.0F};

// Worst-case span of one burst, used to show the rate ceiling a multiplier implies.
auto burst_span_ms(const Types::ChannelConfiguration& Channel) -> int {
    const auto Pairs = static_cast<int>(Channel.ClickMultiplier);
    const auto Gap = static_cast<int>(Channel.MultiplierGap.count());
    return (Pairs - 1) * Gap + Pairs * std::min(Gap / 2, 20);
}
} // namespace

auto Application::initialize() -> bool {
    auto& Configuration = Core::Configuration::instance();
    if (auto Result = Configuration.load(Utils::Paths::config_file()); !Result) {
        set_error(Result.error().describe());
    }
    Config = Configuration.snapshot();

    if (auto Result = Daemon.start(Utils::Paths::runtime_socket()); !Result) {
        set_error(Result.error().describe());
        return false;
    }
    if (auto Result = Daemon.set_configuration(Config); !Result) {
        set_error(Result.error().describe());
    }

    glfwSetErrorCallback(glfw_error);
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    if (glfwInit() == GLFW_FALSE) {
        set_error("Unable to initialize GLFW");
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    Window = glfwCreateWindow(660, 760, "FastClicker", nullptr, nullptr);
    if (Window == nullptr) {
        set_error("Unable to create the FastClicker window");
        return false;
    }
    glfwSetWindowSizeLimits(Window, 500, 400, GLFW_DONT_CARE, GLFW_DONT_CARE);
    OwnWindowId = static_cast<std::uint64_t>(glfwGetX11Window(Window));
    if (auto Result = Daemon.set_own_window(OwnWindowId); !Result) {
        set_error(Result.error().describe());
    }
    if (auto Result = FocusTracker.initialize(); Result) {
        FocusTrackerReady = true;
    } else {
        set_error(Result.error().describe());
    }
    glfwMakeContextCurrent(Window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    auto& Style = ImGui::GetStyle();
    Style.WindowPadding = {16.0F, 14.0F};
    Style.FramePadding = {8.0F, 5.0F};
    Style.ItemSpacing = {9.0F, 7.0F};
    Style.FrameRounding = 4.0F;
    Style.GrabRounding = 4.0F;
    ImGui_ImplGlfw_InitForOpenGL(Window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    NextStatusRefresh = std::chrono::steady_clock::now();
    return true;
}

auto Application::run() -> int {
    Utils::Logger::instance().initialize("gui", Utils::Paths::state_directory() / "gui.log");
    if (!initialize()) return 1;

    while (Running && glfwWindowShouldClose(Window) == GLFW_FALSE) {
        glfwPollEvents();
        Daemon.poll();
        if (auto Error = Daemon.take_error()) set_error(std::move(*Error));
        if (Daemon.exit_requested()) {
            Running = false;
            break;
        }

        const auto Now = std::chrono::steady_clock::now();
        if (Now >= NextStatusRefresh) {
            if (auto Result = Daemon.request_status(); !Result) set_error(Result.error().describe());
            NextStatusRefresh = Now + std::chrono::milliseconds{100};
        }
        if (!ErrorMessage.empty() && Now >= ErrorExpires) ErrorMessage.clear();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        render();
        ImGui::Render();

        auto Width = 0;
        auto Height = 0;
        glfwGetFramebufferSize(Window, &Width, &Height);
        glViewport(0, 0, Width, Height);
        glClearColor(0.045F, 0.052F, 0.066F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(Window);
    }

    LeftEnabled = false;
    RightEnabled = false;
    if (ConfigDirty) {
        auto& Configuration = Core::Configuration::instance();
        if (Configuration.update(Config)) {
            static_cast<void>(Configuration.save(Utils::Paths::config_file()));
        }
    }
    static_cast<void>(Daemon.set_enabled(Types::MouseButton::Left, false));
    static_cast<void>(Daemon.set_enabled(Types::MouseButton::Right, false));
    Daemon.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(Window);
    glfwTerminate();
    return 0;
}

auto Application::render() -> void {
    ImGui::SetNextWindowPos({0.0F, 0.0F}, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    constexpr auto WindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("FastClicker", nullptr, WindowFlags);

    if (const auto& Status = Daemon.cached_status(); Status && Daemon.status_revision() != LastStatusRevision) {
        LeftEnabled = Status->Left.Enabled;
        RightEnabled = Status->Right.Enabled;
        LastStatusRevision = Daemon.status_revision();
    }

    const auto ExitWidth = ImGui::CalcTextSize("Exit").x + ImGui::GetStyle().FramePadding.x * 2.0F;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - ExitWidth));
    if (ImGui::Button("Exit")) Running = false;

    auto Changed = apply_capture();
    Changed |= draw_channel("Left button", Config.Left, Types::MouseButton::Left, LeftEnabled,
                            Types::NumericEditTarget::LeftRate, Types::NumericEditTarget::LeftOffset,
                            Types::NumericEditTarget::LeftStart,
                            Types::NumericEditTarget::LeftMultiplier, Types::NumericEditTarget::LeftGap,
                            Types::BindingTarget::LeftToggle);
    Changed |= draw_channel("Right button", Config.Right, Types::MouseButton::Right, RightEnabled,
                            Types::NumericEditTarget::RightRate, Types::NumericEditTarget::RightOffset,
                            Types::NumericEditTarget::RightStart,
                            Types::NumericEditTarget::RightMultiplier, Types::NumericEditTarget::RightGap,
                            Types::BindingTarget::RightToggle);
    ImGui::TextDisabled("Double-click a bar to type an exact value.");

    ImGui::SeparatorText("Application focus");
    ImGui::Text("Target: %s", Config.TargetApplication ? Config.TargetApplication->c_str() : "Any application");
    if (ImGui::Button("Choose process...")) {
        refresh_application_candidates();
        ImGui::OpenPopup("Choose process");
    }
    if (Config.TargetApplication) {
        ImGui::SameLine();
        if (ImGui::Button("Clear target")) {
            Config.TargetApplication.reset();
            Changed = true;
        }
    }
    if (ImGui::BeginPopupModal("Choose process", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("FastClicker will only generate clicks while this application is active.");
        ImGui::Separator();
        if (ApplicationCandidates.empty()) {
            ImGui::TextDisabled("No running applications were found.");
        }
        for (const auto& Candidate : ApplicationCandidates) {
            const auto Label = Candidate.DisplayName + "###" + Candidate.Identity;
            if (ImGui::Selectable(Label.c_str(), Config.TargetApplication == Candidate.Identity)) {
                Config.TargetApplication = Candidate.Identity;
                Changed = true;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Identity: %s\nPID: %u\n%s", Candidate.Identity.c_str(),
                                  Candidate.ProcessId,
                                  Candidate.Source == Types::ApplicationSource::Window
                                      ? "Has an X11 window: focus is tracked exactly."
                                      : "Wayland-native: gated on the app running, not on focus.");
            }
            if (Candidate.Source != Types::ApplicationSource::Window) {
                ImGui::SameLine();
                ImGui::TextDisabled("(Wayland)");
            }
        }
        ImGui::Separator();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::TextDisabled("The FastClicker window is always blocked.");

    ImGui::SeparatorText("Global shortcuts");
    Changed |= draw_binding("Exit autoclicker", Config.ExitBinding, Types::BindingTarget::Exit);

    if (Changed) {
        ConfigDirty = true;
        ConfigApplyDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{220};
    }
    if (ConfigDirty && std::chrono::steady_clock::now() >= ConfigApplyDeadline) {
        save_and_send();
        ConfigDirty = false;
    }

    ImGui::SeparatorText("Status");
    if (const auto& Status = Daemon.cached_status()) {
        const auto* StateName = Status->Lifecycle == Types::LifecycleState::Enabled
            ? Status->FocusAllowed ? "Enabled" : "Enabled - waiting for focus"
            : "Disabled";
        ImGui::Text("State: %s", StateName);
        draw_channel_status("Left", Status->Left, Config.Left);
        draw_channel_status("Right", Status->Right, Config.Right);
        ImGui::Text("Mouse input: Automatic (%s)", Status->MouseConnected ? "connected" : "waiting");
        if (!Status->ActiveApplication.empty()) {
            ImGui::Text("Focused application: %s", Status->ActiveApplication.c_str());
        }
        if (!Status->FocusTrackingSupported) {
            ImGui::TextColored({1.0F, 0.55F, 0.35F, 1.0F}, "Window focus tracking is unavailable; clicking is blocked.");
        } else if (!Status->FocusObservable && Config.TargetApplication) {
            // Saying "focused application: unknown" would be worse than explaining why it is unknown.
            ImGui::TextColored({1.0F, 0.8F, 0.35F, 1.0F},
                               "A Wayland app is focused; the compositor does not say which.");
            ImGui::TextDisabled(Status->FocusAllowed
                                    ? "Gating on the target being running instead of focused."
                                    : "Clicking paused: the selected application is not running.");
        } else if (!Status->FocusAllowed) {
            ImGui::TextDisabled(Config.TargetApplication
                                    ? "Clicking paused: focus the selected application."
                                    : "Clicking paused: focus another application.");
        }
        if (!Status->LatestError.empty()) {
            ImGui::TextColored({1.0F, 0.55F, 0.35F, 1.0F}, "Backend: %s", Status->LatestError.c_str());
        }
    } else {
        ImGui::TextDisabled("Connecting to backend...");
    }
    if (!ErrorMessage.empty()) {
        ImGui::TextColored({1.0F, 0.35F, 0.35F, 1.0F}, "%s", ErrorMessage.c_str());
    }
    ImGui::End();
}

auto Application::draw_numeric_control(const char* Label, int& Value, const int Minimum,
                                       const int Maximum, const Types::NumericEditTarget Target) -> bool {
    auto Changed = false;
    ImGui::PushID(Label);
    ImGui::TextUnformatted(Label);
    ImGui::SetNextItemWidth(-1.0F);
    if (NumericEditor == Target) {
        if (FocusNumericEditor) {
            ImGui::SetKeyboardFocusHere();
            FocusNumericEditor = false;
        }
        const auto Submitted = ImGui::InputInt("##value", &Value, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
        Value = std::clamp(Value, Minimum, Maximum);
        Changed = ImGui::IsItemEdited();
        if (Submitted || ImGui::IsItemDeactivated()) NumericEditor = Types::NumericEditTarget::None;
    } else {
        Changed = ImGui::SliderInt("##value", &Value, Minimum, Maximum, "%d", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            NumericEditor = Target;
            FocusNumericEditor = true;
        }
    }
    ImGui::PopID();
    return Changed;
}

auto Application::draw_channel(const char* Label, Types::ChannelConfiguration& Channel,
                               const Types::MouseButton Button, bool& EnabledState,
                               const Types::NumericEditTarget RateTarget,
                               const Types::NumericEditTarget OffsetTarget,
                               const Types::NumericEditTarget StartTarget,
                               const Types::NumericEditTarget MultiplierTarget,
                               const Types::NumericEditTarget GapTarget,
                               const Types::BindingTarget BindingTarget) -> bool {
    ImGui::PushID(Label);
    // The header spans the full row, so it must yield hit testing to the checkbox drawn over it.
    const auto Expanded = ImGui::CollapsingHeader(
        Label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    const auto CheckboxWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                               ImGui::CalcTextSize("Enabled").x;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - CheckboxWidth);
    if (ImGui::Checkbox("Enabled", &EnabledState)) {
        // Enabled state is owned by the backend and deliberately never persisted.
        if (auto Result = Daemon.set_enabled(Button, EnabledState); !Result) {
            EnabledState = false;
            set_error(Result.error().describe());
        }
    }
    if (!Expanded) {
        ImGui::PopID();
        return false;
    }

    auto Changed = false;
    ImGui::Indent();
    auto Mode = static_cast<int>(Channel.Mode);
    Changed |= ImGui::RadioButton("Normal", &Mode, static_cast<int>(Types::OperatingMode::Normal));
    ImGui::SameLine();
    Changed |= ImGui::RadioButton("Additive", &Mode, static_cast<int>(Types::OperatingMode::Additive));
    ImGui::SameLine();
    Changed |= ImGui::RadioButton("Hold", &Mode, static_cast<int>(Types::OperatingMode::Hold));
    Channel.Mode = static_cast<Types::OperatingMode>(Mode);
    switch (Channel.Mode) {
    case Types::OperatingMode::Additive:
        ImGui::TextDisabled("Fills physical clicking up to the configured target rate.");
        break;
    case Types::OperatingMode::Hold:
        ImGui::TextDisabled("Clicks at the configured rate while you hold this button.");
        ImGui::TextDisabled("The real button is captured while armed.");
        break;
    default:
        ImGui::TextDisabled("Clicks continuously while enabled.");
        break;
    }

    auto Unit = static_cast<int>(Channel.Unit);
    Changed |= ImGui::RadioButton("Use CPS", &Unit, static_cast<int>(Types::RateUnit::Cps));
    ImGui::SameLine();
    Changed |= ImGui::RadioButton("Use MS", &Unit, static_cast<int>(Types::RateUnit::Milliseconds));
    Channel.Unit = static_cast<Types::RateUnit>(Unit);

    if (Channel.Unit == Types::RateUnit::Cps) {
        auto Cps = static_cast<int>(Channel.Cps);
        Changed |= draw_numeric_control("CPS", Cps, 1, 1000, RateTarget);
        Channel.Cps = static_cast<std::uint16_t>(Cps);
    } else {
        auto Delay = static_cast<int>(Channel.Delay.count());
        Changed |= draw_numeric_control("Delay (MS)", Delay, 1, 10000, RateTarget);
        Channel.Delay = std::chrono::milliseconds{Delay};
    }

    auto Offset = static_cast<int>(Channel.RateOffset);
    const auto OffsetMaximum = Channel.Unit == Types::RateUnit::Cps ? 1000 : 10000;
    Changed |= draw_numeric_control(
        Channel.Unit == Types::RateUnit::Cps ? "Offset (CPS)" : "Offset (MS)",
        Offset, 0, OffsetMaximum, OffsetTarget);
    Channel.RateOffset = static_cast<std::uint16_t>(Offset);

    if (Channel.Unit == Types::RateUnit::Cps) {
        const auto Minimum = std::max(1, static_cast<int>(Channel.Cps) - Offset);
        const auto Maximum = std::min(1000, static_cast<int>(Channel.Cps) + Offset);
        ImGui::TextDisabled("Effective range: %d-%d CPS", Minimum, Maximum);
    } else {
        const auto Base = static_cast<int>(Channel.Delay.count());
        ImGui::TextDisabled("Effective range: %d-%d MS", std::max(1, Base - Offset),
                            std::min(10000, Base + Offset));
    }

    if (Channel.Mode == Types::OperatingMode::Additive) {
        auto Start = static_cast<int>(Channel.AdditiveStartCps);
        Changed |= draw_numeric_control("Additive start (CPS)", Start, 0, 1000, StartTarget);
        Channel.AdditiveStartCps = static_cast<std::uint16_t>(Start);
        ImGui::TextDisabled(Channel.AdditiveStartCps == 0
                                ? "0 always fills in; raise it to only add above a physical rate."
                                : "Adds clicks only while you click at or above this rate.");
    }

    // The additive controller already produces a continuous stream, so bursting is meaningless.
    const auto BurstAllowed = Channel.Mode != Types::OperatingMode::Additive;
    ImGui::BeginDisabled(!BurstAllowed);
    auto Multiplier = static_cast<int>(Channel.ClickMultiplier);
    Changed |= draw_numeric_control("Clicks per click", Multiplier, 1, 10, MultiplierTarget);
    Channel.ClickMultiplier = static_cast<std::uint8_t>(Multiplier);
    if (Channel.ClickMultiplier > 1) {
        auto Gap = static_cast<int>(Channel.MultiplierGap.count());
        Changed |= draw_numeric_control("Gap between clicks (MS)", Gap, 5, 150, GapTarget);
        Channel.MultiplierGap = std::chrono::milliseconds{Gap};
        Changed |= ImGui::Checkbox("Multiply my own clicks too", &Channel.MultiplyPhysicalClicks);
        ImGui::TextDisabled("A %d-click burst spans %d MS, so the rate caps near %d CPS.",
                            Multiplier, burst_span_ms(Channel),
                            std::max(1, 1000 / std::max(1, burst_span_ms(Channel))));
    }
    ImGui::EndDisabled();
    if (!BurstAllowed) ImGui::TextDisabled("Additive mode already fills the gaps for you.");

    Changed |= draw_binding("Toggle this button", Channel.ToggleBinding, BindingTarget);
    ImGui::Unindent();
    ImGui::PopID();
    return Changed;
}

auto Application::draw_channel_status(const char* Label, const Types::ChannelStatus& Channel,
                                      const Types::ChannelConfiguration& Settings) -> void {
    if (!Channel.Enabled) {
        ImGui::TextDisabled("%s: off", Label);
        return;
    }
    if (!Channel.AdditiveGateOpen) {
        ImGui::Text("%s: %.0f physical CPS - waiting for %u CPS", Label, Channel.PhysicalCps,
                    static_cast<unsigned int>(Settings.AdditiveStartCps));
        return;
    }
    ImGui::Text("%s: %.0f physical + %.0f added CPS", Label, Channel.PhysicalCps, Channel.EmittedCps);
}

auto Application::draw_binding(const char* Label, Types::KeyChord& Binding,
                               const Types::BindingTarget Target) -> bool {
    ImGui::PushID(Label);
    ImGui::TextUnformatted(Label);
    const auto Capturing = CaptureTarget == Target;
    if (Capturing) {
        ImGui::BeginDisabled();
        // Deliberately no cancel button: clicking it would itself be captured.
        ImGui::Button("Press any key or mouse button...  (Esc to cancel)", ImVec2{-1.0F, 0.0F});
        ImGui::EndDisabled();
    } else {
        const auto Name = Utils::KeyNames::chord_name(Binding);
        ImGui::BeginDisabled(CaptureTarget != Types::BindingTarget::None);
        if (ImGui::Button(Name.c_str(), ImVec2{-1.0F, 0.0F})) {
            CaptureTarget = Target;
            CaptureToken = NextCaptureToken++;
            if (auto Result = Daemon.begin_binding_capture(CaptureToken); !Result) {
                CaptureTarget = Types::BindingTarget::None;
                set_error(Result.error().describe());
            }
        }
        ImGui::EndDisabled();
    }
    if (Utils::KeyNames::is_mouse_button(Binding.KeyCode)) {
        ImGui::TextColored(WarningColour, "Every %s click will trigger this.",
                           Utils::KeyNames::display_name(Binding.KeyCode).c_str());
    }
    ImGui::PopID();
    // The binding is written by apply_capture when the daemon reports the result.
    return false;
}

auto Application::apply_capture() -> bool {
    const auto Capture = Daemon.take_capture();
    if (!Capture) return false;
    // A result for a capture this widget already abandoned is harmless to drop.
    if (Capture->Token != CaptureToken || CaptureTarget == Types::BindingTarget::None) return false;
    const auto Target = std::exchange(CaptureTarget, Types::BindingTarget::None);
    if (Capture->Outcome != Types::CaptureOutcome::Captured) return false;
    switch (Target) {
    case Types::BindingTarget::LeftToggle: Config.Left.ToggleBinding = Capture->Chord; break;
    case Types::BindingTarget::RightToggle: Config.Right.ToggleBinding = Capture->Chord; break;
    case Types::BindingTarget::Exit: Config.ExitBinding = Capture->Chord; break;
    default: return false;
    }
    return true;
}

auto Application::refresh_application_candidates() -> void {
    // Processes first: on Wayland this is the only source that sees a native client, which owns no
    // X11 toplevel. X11 windows are then merged in, upgrading an entry to focus-trackable.
    Inventory.refresh();
    ApplicationCandidates = Inventory.applications();

    if (FocusTrackerReady) {
        for (auto& Toplevel : FocusTracker.windows()) {
            if (Toplevel.WindowId == OwnWindowId) continue;
            auto Identity = Inventory.identify(Toplevel.ProcessId);
            if (Identity.empty()) Identity = Toplevel.WindowClass;
            const auto Existing = std::ranges::find(ApplicationCandidates, Identity,
                                                    &Types::ApplicationCandidate::Identity);
            if (Existing != ApplicationCandidates.end()) {
                Existing->WindowClass = std::move(Toplevel.WindowClass);
                Existing->WindowId = Toplevel.WindowId;
                Existing->Source = Types::ApplicationSource::Window;
                continue;
            }
            // An X11 window the inventory did not see belongs to something the session did not
            // launch as an application, such as a game started straight from a terminal.
            Toplevel.Identity = std::move(Identity);
            ApplicationCandidates.push_back(std::move(Toplevel));
        }
    }
    Input::ApplicationInventory::sort_candidates(ApplicationCandidates);
}

auto Application::save_and_send() -> void {
    auto& Configuration = Core::Configuration::instance();
    if (auto Result = Configuration.update(Config); !Result) {
        set_error(Result.error().describe());
        return;
    }
    if (auto Result = Configuration.save(Utils::Paths::config_file()); !Result) {
        set_error(Result.error().describe());
    }
    if (auto Result = Daemon.set_configuration(Config); !Result) {
        set_error(Result.error().describe());
    }
}

auto Application::set_error(std::string Message) -> void {
    ErrorMessage = std::move(Message);
    ErrorExpires = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    Utils::Logger::instance().log(Types::LogLevel::Error, ErrorMessage);
}

} // namespace Autoclicker::Gui
