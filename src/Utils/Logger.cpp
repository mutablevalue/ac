#include "Utils/Logger.hpp"

#include <chrono>
#include <format>
#include <iostream>

namespace Autoclicker::Utils {
namespace {
constexpr auto MaxLogSize = std::uintmax_t{1024 * 1024};

auto level_name(const Types::LogLevel Level) -> std::string_view {
    switch (Level) {
    case Types::LogLevel::Trace: return "TRACE";
    case Types::LogLevel::Debug: return "DEBUG";
    case Types::LogLevel::Info: return "INFO";
    case Types::LogLevel::Warning: return "WARN";
    case Types::LogLevel::Error: return "ERROR";
    case Types::LogLevel::Critical: return "CRITICAL";
    }
    return "UNKNOWN";
}
} // namespace

auto Logger::instance() -> Logger& {
    static Logger Instance;
    return Instance;
}

auto Logger::initialize(const std::string_view Name, const std::filesystem::path& Path) -> void {
    const auto Lock = std::scoped_lock{Mutex};
    ProcessName = Name;
    FilePath = Path;
    std::error_code ErrorValue;
    std::filesystem::create_directories(FilePath.parent_path(), ErrorValue);
    rotate_if_needed();
    Stream.open(FilePath, std::ios::app);
}

auto Logger::set_level(const Types::LogLevel Level) noexcept -> void { MinimumLevel = Level; }

auto Logger::log(const Types::LogLevel Level, const std::string_view Message,
                 const std::source_location Location) -> void {
    if (Level < MinimumLevel) {
        return;
    }
    const auto Lock = std::scoped_lock{Mutex};
    const auto Now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    const auto Line = std::format("{:%F %T} [{}] [{}] {} ({}:{})\n", Now, ProcessName,
                                  level_name(Level), Message, Location.file_name(), Location.line());
    std::cerr << Line;
    if (Stream.is_open()) {
        Stream << Line;
        if (Level >= Types::LogLevel::Error) {
            Stream.flush();
        }
    }
}

auto Logger::flush() -> void {
    const auto Lock = std::scoped_lock{Mutex};
    if (Stream.is_open()) {
        Stream.flush();
    }
}

auto Logger::rotate_if_needed() -> void {
    std::error_code ErrorValue;
    if (!std::filesystem::exists(FilePath, ErrorValue) ||
        std::filesystem::file_size(FilePath, ErrorValue) < MaxLogSize) {
        return;
    }
    const auto Backup = FilePath.string() + ".1";
    std::filesystem::remove(Backup, ErrorValue);
    std::filesystem::rename(FilePath, Backup, ErrorValue);
}

} // namespace Autoclicker::Utils
