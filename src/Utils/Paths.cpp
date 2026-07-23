#include "Utils/Paths.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <unistd.h>

namespace Autoclicker::Utils {
namespace {
auto environment_path(const char* Name) -> std::filesystem::path {
    if (const auto* Value = std::getenv(Name); Value != nullptr && std::string_view{Value}.size() > 0) {
        return Value;
    }
    return {};
}

auto home_path() -> std::filesystem::path {
    auto HomePath = environment_path("HOME");
    if (HomePath.empty()) {
        throw std::runtime_error{"HOME is not set"};
    }
    return HomePath;
}
} // namespace

auto Paths::config_file() -> std::filesystem::path {
    auto Base = environment_path("XDG_CONFIG_HOME");
    if (Base.empty()) Base = home_path() / ".config";
    return Base / "fastclicker" / "config.json";
}

auto Paths::state_directory() -> std::filesystem::path {
    auto Base = environment_path("XDG_STATE_HOME");
    if (Base.empty()) Base = home_path() / ".local" / "state";
    return Base / "fastclicker";
}

auto Paths::runtime_socket() -> std::filesystem::path {
    auto Base = environment_path("XDG_RUNTIME_DIR");
    if (Base.empty()) Base = std::filesystem::path{"/tmp"} / ("fastclicker-" + std::to_string(getuid()));
    return Base / "fastclicker.sock";
}

} // namespace Autoclicker::Utils
