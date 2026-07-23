#pragma once

#include "Types/ErrorTypes.hpp"

#include <source_location>
#include <string>
#include <system_error>

namespace Autoclicker::Utils {

class Error final {
public:
    Error(Types::ErrorCode Code, std::string Message,
          std::error_code SystemError = {},
          std::source_location Location = std::source_location::current());

    [[nodiscard]] auto code() const noexcept -> Types::ErrorCode;
    [[nodiscard]] auto message() const noexcept -> const std::string&;
    [[nodiscard]] auto system_error() const noexcept -> const std::error_code&;
    [[nodiscard]] auto location() const noexcept -> const std::source_location&;
    [[nodiscard]] auto describe() const -> std::string;

private:
    Types::ErrorCode ErrorCodeValue;
    std::string MessageText;
    std::error_code SystemErrorValue;
    std::source_location SourceLocation;
};

class ErrorHandler final {
public:
    static auto report(const Error& Value) -> void;
};

} // namespace Autoclicker::Utils
