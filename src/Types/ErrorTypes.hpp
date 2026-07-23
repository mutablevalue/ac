#pragma once

namespace Autoclicker::Types {

enum class ErrorCode {
    InvalidConfiguration,
    Io,
    PermissionDenied,
    DeviceUnavailable,
    Protocol,
    Process,
    Internal,
};

} // namespace Autoclicker::Types

