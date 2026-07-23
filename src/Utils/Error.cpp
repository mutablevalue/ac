#include "Utils/Error.hpp"

#include "Utils/Logger.hpp"

#include <format>

namespace Autoclicker::Utils {

Error::Error(const Types::ErrorCode Code, std::string Message,
             std::error_code SystemError, const std::source_location Location)
    : ErrorCodeValue(Code), MessageText(std::move(Message)), SystemErrorValue(SystemError),
      SourceLocation(Location) {}

auto Error::code() const noexcept -> Types::ErrorCode { return ErrorCodeValue; }
auto Error::message() const noexcept -> const std::string& { return MessageText; }
auto Error::system_error() const noexcept -> const std::error_code& { return SystemErrorValue; }
auto Error::location() const noexcept -> const std::source_location& { return SourceLocation; }

auto Error::describe() const -> std::string {
    if (SystemErrorValue) {
        return std::format("{}: {} ({}:{})", MessageText, SystemErrorValue.message(),
                           SourceLocation.file_name(), SourceLocation.line());
    }
    return std::format("{} ({}:{})", MessageText, SourceLocation.file_name(), SourceLocation.line());
}

auto ErrorHandler::report(const Error& Value) -> void {
    Logger::instance().log(Types::LogLevel::Error, Value.describe());
}

} // namespace Autoclicker::Utils
