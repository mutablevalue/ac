#pragma once

#include "Types/LogTypes.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <source_location>
#include <string_view>

namespace Autoclicker::Utils {

class Logger final {
public:
    static auto instance() -> Logger&;

    Logger(const Logger&) = delete;
    auto operator=(const Logger&) -> Logger& = delete;

    auto initialize(std::string_view ProcessName, const std::filesystem::path& FilePath) -> void;
    auto set_level(Types::LogLevel MinimumLevel) noexcept -> void;
    auto log(Types::LogLevel Level, std::string_view Message,
             std::source_location Location = std::source_location::current()) -> void;
    auto flush() -> void;

private:
    Logger() = default;
    auto rotate_if_needed() -> void;

    std::mutex Mutex;
    std::ofstream Stream;
    std::filesystem::path FilePath;
    std::string ProcessName{"autoclicker"};
    Types::LogLevel MinimumLevel{Types::LogLevel::Info};
};

} // namespace Autoclicker::Utils
