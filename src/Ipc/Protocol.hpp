#pragma once

#include "Types/IpcTypes.hpp"
#include "Utils/Error.hpp"

#include <expected>
#include <string>
#include <string_view>

namespace Autoclicker::Ipc {

class Protocol final {
public:
    [[nodiscard]] static auto encode(const Types::IpcMessage& Message) -> std::string;
    [[nodiscard]] static auto decode(std::string_view Payload)
        -> std::expected<Types::IpcMessage, Utils::Error>;
};

} // namespace Autoclicker::Ipc

