#include "Ipc/Socket.hpp"

#include "Ipc/Protocol.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace Autoclicker::Ipc {
namespace {
auto socket_error(const std::string& Message) -> Utils::Error {
    return {errno == EACCES ? Types::ErrorCode::PermissionDenied : Types::ErrorCode::Io,
            Message, std::error_code{errno, std::generic_category()}};
}

auto address_for(const std::filesystem::path& Path) -> std::expected<sockaddr_un, Utils::Error> {
    const auto Text = Path.string();
    if (Text.size() >= sizeof(sockaddr_un::sun_path)) {
        return std::unexpected{Utils::Error{Types::ErrorCode::Io, "Runtime socket path is too long"}};
    }
    auto Address = sockaddr_un{};
    Address.sun_family = AF_UNIX;
    std::memcpy(Address.sun_path, Text.c_str(), Text.size() + 1);
    return Address;
}

} // namespace

Socket::Socket(Utils::UniqueFd Descriptor) : DescriptorValue(std::move(Descriptor)) {}

auto Socket::listen(const std::filesystem::path& Path) -> std::expected<Socket, Utils::Error> {
    std::error_code FileError;
    std::filesystem::create_directories(Path.parent_path(), FileError);
    auto Descriptor = Utils::UniqueFd{socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0)};
    if (!Descriptor) return std::unexpected{socket_error("Unable to create IPC socket")};
    const auto Address = address_for(Path);
    if (!Address) return std::unexpected{Address.error()};
    if (bind(Descriptor.get(), reinterpret_cast<const sockaddr*>(&*Address), sizeof(*Address)) < 0) {
        return std::unexpected{socket_error("Unable to bind IPC socket")};
    }
    if (chmod(Path.c_str(), 0600) < 0 || ::listen(Descriptor.get(), 1) < 0) {
        return std::unexpected{socket_error("Unable to secure or listen on IPC socket")};
    }
    return Socket{std::move(Descriptor)};
}

auto Socket::connect(const std::filesystem::path& Path) -> std::expected<Socket, Utils::Error> {
    auto Descriptor = Utils::UniqueFd{socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0)};
    if (!Descriptor) return std::unexpected{socket_error("Unable to create IPC socket")};
    const auto Address = address_for(Path);
    if (!Address) return std::unexpected{Address.error()};
    if (::connect(Descriptor.get(), reinterpret_cast<const sockaddr*>(&*Address), sizeof(*Address)) < 0) {
        return std::unexpected{socket_error("Unable to connect to daemon")};
    }
    return Socket{std::move(Descriptor)};
}

auto Socket::accept_same_user() const -> std::expected<Socket, Utils::Error> {
    auto Client = Utils::UniqueFd{accept4(DescriptorValue.get(), nullptr, nullptr, SOCK_CLOEXEC)};
    if (!Client) return std::unexpected{socket_error("Unable to accept GUI connection")};
    auto Credentials = ucred{};
    auto Size = socklen_t{sizeof(Credentials)};
    if (getsockopt(Client.get(), SOL_SOCKET, SO_PEERCRED, &Credentials, &Size) < 0 || Credentials.uid != getuid()) {
        return std::unexpected{Utils::Error{Types::ErrorCode::PermissionDenied, "Rejected IPC peer with a different user ID"}};
    }
    return Socket{std::move(Client)};
}

auto Socket::send(const Types::IpcMessage& Message) const -> std::expected<void, Utils::Error> {
    const auto Payload = Protocol::encode(Message);
    const auto Count = ::send(DescriptorValue.get(), Payload.data(), Payload.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
    if (Count < 0 || static_cast<std::size_t>(Count) != Payload.size()) {
        return std::unexpected{socket_error("Unable to send IPC message")};
    }
    return {};
}

auto Socket::receive() const -> std::expected<Types::IpcMessage, Utils::Error> {
    auto Buffer = std::array<char, 64 * 1024>{};
    const auto Count = recv(DescriptorValue.get(), Buffer.data(), Buffer.size(), 0);
    if (Count == 0) return std::unexpected{Utils::Error{Types::ErrorCode::Protocol, "IPC peer disconnected"}};
    if (Count < 0) return std::unexpected{socket_error("Unable to receive IPC message")};
    return Protocol::decode(std::string_view{Buffer.data(), static_cast<std::size_t>(Count)});
}

auto Socket::descriptor() const noexcept -> int { return DescriptorValue.get(); }
Socket::operator bool() const noexcept { return static_cast<bool>(DescriptorValue); }

} // namespace Autoclicker::Ipc
