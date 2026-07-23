#pragma once

#include <filesystem>

namespace Autoclicker::Utils {

class Paths final {
public:
    [[nodiscard]] static auto config_file() -> std::filesystem::path;
    [[nodiscard]] static auto state_directory() -> std::filesystem::path;
    [[nodiscard]] static auto runtime_socket() -> std::filesystem::path;
};

} // namespace Autoclicker::Utils

