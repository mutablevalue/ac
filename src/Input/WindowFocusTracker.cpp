#include "Input/WindowFocusTracker.hpp"

#include <algorithm>
#include <cerrno>
#include <ranges>
#include <string_view>
#include <system_error>
#include <utility>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace Autoclicker::Input {
namespace {
auto focus_error(const std::string& Message) -> Utils::Error {
    return {Types::ErrorCode::DeviceUnavailable, Message,
            errno == 0 ? std::error_code{} : std::error_code{errno, std::generic_category()}};
}
} // namespace

class WindowFocusTracker::Impl final {
public:
    ~Impl() {
        if (DisplayHandle != nullptr) XCloseDisplay(DisplayHandle);
    }

    auto initialize() -> std::expected<void, Utils::Error> {
        if (DisplayHandle != nullptr) return {};
        DisplayHandle = XOpenDisplay(nullptr);
        if (DisplayHandle == nullptr) {
            return std::unexpected{focus_error("Unable to connect to X11/XWayland for window focus tracking")};
        }
        RootWindow = DefaultRootWindow(DisplayHandle);
        ActiveWindowAtom = XInternAtom(DisplayHandle, "_NET_ACTIVE_WINDOW", False);
        ClientListAtom = XInternAtom(DisplayHandle, "_NET_CLIENT_LIST", False);
        WindowNameAtom = XInternAtom(DisplayHandle, "_NET_WM_NAME", False);
        ProcessIdAtom = XInternAtom(DisplayHandle, "_NET_WM_PID", False);
        XSelectInput(DisplayHandle, RootWindow, PropertyChangeMask);
        XFlush(DisplayHandle);
        return {};
    }

    auto drain_changes() -> bool {
        if (DisplayHandle == nullptr) return false;
        auto ActiveWindowChanged = false;
        while (XPending(DisplayHandle) > 0) {
            auto Event = XEvent{};
            XNextEvent(DisplayHandle, &Event);
            ActiveWindowChanged = ActiveWindowChanged ||
                (Event.type == PropertyNotify && Event.xproperty.atom == ActiveWindowAtom);
        }
        return ActiveWindowChanged;
    }

    [[nodiscard]] auto window_property(const Window WindowValue) const -> std::vector<Window> {
        if (DisplayHandle == nullptr) return {};
        auto ActualType = Atom{};
        auto ActualFormat = 0;
        auto ItemCount = 0UL;
        auto BytesAfter = 0UL;
        unsigned char* Data{};
        const auto Result = XGetWindowProperty(DisplayHandle, WindowValue, ClientListAtom, 0, 4096,
                                               False, XA_WINDOW, &ActualType, &ActualFormat,
                                               &ItemCount, &BytesAfter, &Data);
        if (Result != Success || Data == nullptr || ActualType != XA_WINDOW || ActualFormat != 32) {
            if (Data != nullptr) XFree(Data);
            return {};
        }
        const auto* Values = reinterpret_cast<const Window*>(Data);
        auto Windows = std::vector<Window>{Values, Values + ItemCount};
        XFree(Data);
        return Windows;
    }

    [[nodiscard]] auto active_window() const -> Window {
        if (DisplayHandle == nullptr) return None;
        auto ActualType = Atom{};
        auto ActualFormat = 0;
        auto ItemCount = 0UL;
        auto BytesAfter = 0UL;
        unsigned char* Data{};
        const auto Result = XGetWindowProperty(DisplayHandle, RootWindow, ActiveWindowAtom, 0, 1,
                                               False, XA_WINDOW, &ActualType, &ActualFormat,
                                               &ItemCount, &BytesAfter, &Data);
        auto ResultWindow = Window{None};
        if (Result == Success && Data != nullptr && ActualType == XA_WINDOW &&
            ActualFormat == 32 && ItemCount == 1) {
            ResultWindow = *reinterpret_cast<const Window*>(Data);
        }
        if (Data != nullptr) XFree(Data);
        return ResultWindow;
    }

    [[nodiscard]] auto application_name(const Window WindowValue) const -> std::string {
        if (DisplayHandle == nullptr || WindowValue == None) return {};
        auto Hint = XClassHint{};
        if (XGetClassHint(DisplayHandle, WindowValue, &Hint) == 0) return {};
        auto Result = Hint.res_class != nullptr ? std::string{Hint.res_class}
                                                : Hint.res_name != nullptr ? std::string{Hint.res_name}
                                                                           : std::string{};
        if (Hint.res_name != nullptr) XFree(Hint.res_name);
        if (Hint.res_class != nullptr) XFree(Hint.res_class);
        return Result;
    }

    [[nodiscard]] auto window_title(const Window WindowValue) const -> std::string {
        if (DisplayHandle == nullptr) return {};
        auto Text = XTextProperty{};
        if (XGetTextProperty(DisplayHandle, WindowValue, &Text, WindowNameAtom) != 0 && Text.value != nullptr) {
            auto Result = std::string{reinterpret_cast<const char*>(Text.value), Text.nitems};
            XFree(Text.value);
            return Result;
        }
        char* Name{};
        if (XFetchName(DisplayHandle, WindowValue, &Name) != 0 && Name != nullptr) {
            auto Result = std::string{Name};
            XFree(Name);
            return Result;
        }
        return {};
    }

    [[nodiscard]] auto process_id(const Window WindowValue) const -> std::uint32_t {
        auto ActualType = Atom{};
        auto ActualFormat = 0;
        auto ItemCount = 0UL;
        auto BytesAfter = 0UL;
        unsigned char* Data{};
        const auto Result = XGetWindowProperty(DisplayHandle, WindowValue, ProcessIdAtom, 0, 1,
                                               False, XA_CARDINAL, &ActualType, &ActualFormat,
                                               &ItemCount, &BytesAfter, &Data);
        auto ProcessId = std::uint32_t{};
        if (Result == Success && Data != nullptr && ActualType == XA_CARDINAL &&
            ActualFormat == 32 && ItemCount == 1) {
            ProcessId = static_cast<std::uint32_t>(*reinterpret_cast<const unsigned long*>(Data));
        }
        if (Data != nullptr) XFree(Data);
        return ProcessId;
    }

    [[nodiscard]] auto windows() const -> std::vector<Types::ApplicationCandidate> {
        auto Result = std::vector<Types::ApplicationCandidate>{};
        for (const auto WindowValue : window_property(RootWindow)) {
            auto Attributes = XWindowAttributes{};
            if (XGetWindowAttributes(DisplayHandle, WindowValue, &Attributes) == 0 ||
                Attributes.map_state != IsViewable) {
                continue;
            }
            auto Application = application_name(WindowValue);
            if (Application.empty()) continue;
            auto Title = window_title(WindowValue);
            Result.push_back({.Identity = Application,
                              .DisplayName = Title.empty() ? Application : std::move(Title),
                              .WindowClass = std::move(Application),
                              .WindowId = static_cast<std::uint64_t>(WindowValue),
                              .ProcessId = process_id(WindowValue),
                              .Source = Types::ApplicationSource::Window});
        }
        std::ranges::sort(Result, {}, &Types::ApplicationCandidate::Identity);
        const auto Duplicate = std::ranges::unique(Result, {}, &Types::ApplicationCandidate::Identity);
        Result.erase(Duplicate.begin(), Duplicate.end());
        return Result;
    }

    Display* DisplayHandle{};
    Window RootWindow{None};
    Atom ActiveWindowAtom{None};
    Atom ClientListAtom{None};
    Atom WindowNameAtom{None};
    Atom ProcessIdAtom{None};
};

WindowFocusTracker::WindowFocusTracker() : Implementation(std::make_unique<Impl>()) {}
WindowFocusTracker::~WindowFocusTracker() = default;
WindowFocusTracker::WindowFocusTracker(WindowFocusTracker&&) noexcept = default;
auto WindowFocusTracker::operator=(WindowFocusTracker&&) noexcept -> WindowFocusTracker& = default;

auto WindowFocusTracker::initialize() -> std::expected<void, Utils::Error> {
    return Implementation->initialize();
}

auto WindowFocusTracker::drain_changes() -> bool { return Implementation->drain_changes(); }

auto WindowFocusTracker::windows() const -> std::vector<Types::ApplicationCandidate> {
    return Implementation->windows();
}

auto WindowFocusTracker::descriptor() const noexcept -> int {
    return Implementation->DisplayHandle == nullptr ? -1 : ConnectionNumber(Implementation->DisplayHandle);
}

auto WindowFocusTracker::focus_state(const std::uint64_t OwnWindowId) const -> Types::WindowFocusState {
    const auto ActiveWindow = Implementation->active_window();
    if (ActiveWindow == None) return {};
    auto ActiveWindowClass = Implementation->application_name(ActiveWindow);
    const auto ActiveProcessId = Implementation->process_id(ActiveWindow);
    const auto OwnWindowFocused = OwnWindowId != 0 &&
        static_cast<std::uint64_t>(ActiveWindow) == OwnWindowId;
    // Mutter points _NET_ACTIVE_WINDOW at an internal proxy window carrying neither WM_CLASS nor
    // _NET_WM_PID whenever a Wayland-native client holds focus. That window identifies nothing, so
    // report focus as unobservable rather than as "some application with an empty name".
    const auto FocusObservable = OwnWindowFocused || !ActiveWindowClass.empty() || ActiveProcessId != 0;
    return {.Supported = true, .OwnWindowFocused = OwnWindowFocused, .FocusObservable = FocusObservable,
            .ActiveWindowId = static_cast<std::uint64_t>(ActiveWindow),
            .ActiveProcessId = ActiveProcessId, .ActiveWindowClass = std::move(ActiveWindowClass)};
}

} // namespace Autoclicker::Input
