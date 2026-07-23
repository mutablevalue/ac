#include "Gui/Application.hpp"

#include "Core/Configuration.hpp"
#include "Utils/Logger.hpp"
#include "Utils/Paths.hpp"

#include <algorithm>
#include <array>
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

constexpr auto BindableKeys = std::array{
    std::pair{KEY_F1, "F1"}, std::pair{KEY_F2, "F2"}, std::pair{KEY_F3, "F3"},
    std::pair{KEY_F4, "F4"}, std::pair{KEY_F5, "F5"}, std::pair{KEY_F6, "F6"},
    std::pair{KEY_F7, "F7"}, std::pair{KEY_F8, "F8"}, std::pair{KEY_F9, "F9"},
    std::pair{KEY_F10, "F10"}, std::pair{KEY_F11, "F11"}, std::pair{KEY_F12, "F12"},
    std::pair{KEY_INSERT, "Insert"}, std::pair{KEY_DELETE, "Delete"},
    std::pair{KEY_HOME, "Home"}, std::pair{KEY_END, "End"},
};
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
    Window = glfwCreateWindow(660, 620, "FastClicker", nullptr, nullptr);
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

    Enabled = false;
    if (ConfigDirty) {
        auto& Configuration = Core::Configuration::instance();
        if (Configuration.update(Config)) {
            static_cast<void>(Configuration.save(Utils::Paths::config_file()));
        }
    }
    static_cast<void>(Daemon.set_enabled(false));
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
        Enabled = Status->Lifecycle == Types::LifecycleState::Enabled;
        LastStatusRevision = Daemon.status_revision();
    }

    ImGui::SetNextItemWidth(120.0F);
    if (ImGui::Checkbox("Enabled", &Enabled)) {
        if (auto Result = Daemon.set_enabled(Enabled); !Result) {
            Enabled = false;
            set_error(Result.error().describe());
        }
    }
    ImGui::SameLine();
    const auto ExitWidth = ImGui::CalcTextSize("Exit").x + ImGui::GetStyle().FramePadding.x * 2.0F;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - ExitWidth));
    if (ImGui::Button("Exit")) Running = false;

    ImGui::SeparatorText("Click behavior");
    auto Changed = false;
    auto Mode = static_cast<int>(Config.Mode);
    Changed |= ImGui::RadioButton("Normal", &Mode, static_cast<int>(Types::OperatingMode::Normal));
    ImGui::SameLine();
    Changed |= ImGui::RadioButton("Additive", &Mode, static_cast<int>(Types::OperatingMode::Additive));
    Config.Mode = static_cast<Types::OperatingMode>(Mode);
    ImGui::TextDisabled(Config.Mode == Types::OperatingMode::Normal
                            ? "Clicks continuously while enabled."
                            : "Fills physical clicking up to the configured target rate.");

    auto Unit = static_cast<int>(Config.Unit);
    Changed |= ImGui::RadioButton("Use CPS", &Unit, static_cast<int>(Types::RateUnit::Cps));
    ImGui::SameLine();
    Changed |= ImGui::RadioButton("Use MS", &Unit, static_cast<int>(Types::RateUnit::Milliseconds));
    Config.Unit = static_cast<Types::RateUnit>(Unit);

    if (Config.Unit == Types::RateUnit::Cps) {
        auto Cps = static_cast<int>(Config.Cps);
        Changed |= draw_numeric_control("CPS", Cps, 1, 1000, Types::NumericEditTarget::Rate);
        Config.Cps = static_cast<std::uint16_t>(Cps);
    } else {
        auto Delay = static_cast<int>(Config.Delay.count());
        Changed |= draw_numeric_control("Delay (MS)", Delay, 1, 10000, Types::NumericEditTarget::Rate);
        Config.Delay = std::chrono::milliseconds{Delay};
    }

    auto Offset = static_cast<int>(Config.RateOffset);
    const auto OffsetMaximum = Config.Unit == Types::RateUnit::Cps ? 1000 : 10000;
    Changed |= draw_numeric_control(
        Config.Unit == Types::RateUnit::Cps ? "Offset (CPS)" : "Offset (MS)",
        Offset, 0, OffsetMaximum, Types::NumericEditTarget::Offset);
    Config.RateOffset = static_cast<std::uint16_t>(Offset);

    if (Config.Unit == Types::RateUnit::Cps) {
        const auto Minimum = std::max(1, static_cast<int>(Config.Cps) - Offset);
        const auto Maximum = std::min(1000, static_cast<int>(Config.Cps) + Offset);
        ImGui::TextDisabled("Effective range: %d-%d CPS", Minimum, Maximum);
    } else {
        const auto Base = static_cast<int>(Config.Delay.count());
        ImGui::TextDisabled("Effective range: %d-%d MS", std::max(1, Base - Offset),
                            std::min(10000, Base + Offset));
    }
    ImGui::TextDisabled("Double-click a bar to type an exact value.");

    ImGui::SeparatorText("Application focus");
    ImGui::Text("Target: %s", Config.TargetApplication ? Config.TargetApplication->c_str() : "Any application");
    if (ImGui::Button("Choose process...")) {
        refresh_window_candidates();
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
        ImGui::TextUnformatted("FastClicker will only generate clicks while this application is focused.");
        ImGui::Separator();
        if (WindowCandidates.empty()) {
            ImGui::TextDisabled("No selectable X11/XWayland applications are currently open.");
        }
        for (const auto& Candidate : WindowCandidates) {
            const auto Label = Candidate.Title.empty()
                ? Candidate.Application
                : Candidate.Title + "###" + Candidate.Application;
            if (ImGui::Selectable(Label.c_str(), Config.TargetApplication == Candidate.Application)) {
                Config.TargetApplication = Candidate.Application;
                Changed = true;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Application: %s\nPID: %u", Candidate.Application.c_str(), Candidate.ProcessId);
            }
        }
        ImGui::Separator();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::TextDisabled("The FastClicker window is always blocked.");

    ImGui::SeparatorText("Global shortcuts");
    Changed |= draw_binding("Toggle autoclicker", Config.ToggleBinding);
    Changed |= draw_binding("Exit autoclicker", Config.ExitBinding);

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
        ImGui::Text("Physical: %.0f CPS    Added: %.0f CPS", Status->PhysicalCps, Status->EmittedCps);
        ImGui::Text("Mouse input: Automatic (%s)", Status->MouseConnected ? "connected" : "waiting");
        if (!Status->ActiveApplication.empty()) {
            ImGui::Text("Focused application: %s", Status->ActiveApplication.c_str());
        }
        if (!Status->FocusTrackingSupported) {
            ImGui::TextColored({1.0F, 0.55F, 0.35F, 1.0F}, "Window focus tracking is unavailable; clicking is blocked.");
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

auto Application::draw_binding(const char* Label, Types::KeyChord& Binding) -> bool {
    auto Changed = false;
    const auto Current = std::ranges::find_if(
        BindableKeys, [&Binding](const auto& Pair) { return Pair.first == Binding.KeyCode; });
    const auto* Preview = Current == BindableKeys.end() ? "Unknown" : Current->second;
    ImGui::PushID(Label);
    ImGui::TextUnformatted(Label);
    ImGui::SetNextItemWidth(120.0F);
    if (ImGui::BeginCombo("##key", Preview)) {
        for (const auto& [Code, Name] : BindableKeys) {
            if (ImGui::Selectable(Name, Binding.KeyCode == static_cast<std::uint16_t>(Code))) {
                Binding.KeyCode = static_cast<std::uint16_t>(Code);
                Changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    Changed |= ImGui::Checkbox("Ctrl", &Binding.Control);
    ImGui::SameLine();
    Changed |= ImGui::Checkbox("Alt", &Binding.Alt);
    ImGui::SameLine();
    Changed |= ImGui::Checkbox("Shift", &Binding.Shift);
    ImGui::SameLine();
    Changed |= ImGui::Checkbox("Super", &Binding.Super);
    ImGui::PopID();
    return Changed;
}

auto Application::refresh_window_candidates() -> void {
    if (!FocusTrackerReady) {
        WindowCandidates.clear();
        return;
    }
    WindowCandidates = FocusTracker.windows();
    std::erase_if(WindowCandidates, [this](const Types::WindowCandidate& Candidate) {
        return Candidate.WindowId == OwnWindowId;
    });
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
