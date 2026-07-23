#pragma once

#include "Types/IpcTypes.hpp"
#include "Utils/Error.hpp"
#include "Utils/UniqueFd.hpp"

#include <expected>
#include <filesystem>

namespace Autoclicker::Ipc {

class Socket final {
public:
    Socket() = default;
    explicit Socket(Utils::UniqueFd Descriptor);

    [[nodiscard]] static auto listen(const std::filesystem::path& Path)
        -> std::expected<Socket, Utils::Error>;
    [[nodiscard]] static auto connect(const std::filesystem::path& Path)
        -> std::expected<Socket, Utils::Error>;
    [[nodiscard]] auto accept_same_user() const -> std::expected<Socket, Utils::Error>;

    auto send(const Types::IpcMessage& Message) const -> std::expected<void, Utils::Error>;
    [[nodiscard]] auto receive() const -> std::expected<Types::IpcMessage, Utils::Error>;
    [[nodiscard]] auto descriptor() const noexcept -> int;
    [[nodiscard]] explicit operator bool() const noexcept;

private:
    Utils::UniqueFd DescriptorValue;
};

} // namespace Autoclicker::Ipc
