#pragma once

#include <cstdint>
#include <string>

namespace Autoclicker::Types {

struct WindowCandidate final {
    std::uint64_t WindowId{};
    std::uint32_t ProcessId{};
    std::string Application;
    std::string Title;
};

struct WindowFocusState final {
    bool Supported{};
    bool OwnWindowFocused{};
    bool TargetFocused{};
    std::string ActiveApplication;
};

} // namespace Autoclicker::Types
