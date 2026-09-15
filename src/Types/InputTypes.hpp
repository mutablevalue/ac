#pragma once

#include <cstdint>
#include <string>

namespace Autoclicker::Types {

enum class InputDeviceKind { Mouse, Keyboard };
enum class MouseButton { Left, Right };
enum class HotkeyAction { ToggleLeftClicker, ToggleRightClicker, ExitAutoclicker };
enum class CaptureOutcome { Captured, Cancelled, TimedOut };

struct InputDeviceInfo final {
    std::string Id;
    std::string Name;
    std::string DeviceNode;
    InputDeviceKind Kind{InputDeviceKind::Mouse};
    bool Connected{};
};

struct RawInputEvent final {
    std::string DeviceId;
    InputDeviceKind Kind{InputDeviceKind::Keyboard};
    std::uint16_t Type{};
    std::uint16_t Code{};
    std::int32_t Value{};
};

} // namespace Autoclicker::Types
