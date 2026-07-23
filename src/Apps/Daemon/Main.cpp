#include "Core/Daemon.hpp"
#include "Utils/Error.hpp"
#include "Utils/Logger.hpp"
#include "Utils/Paths.hpp"

#include <filesystem>
#include <string_view>

auto main(const int ArgumentCount, char** ArgumentValues) -> int {
    using namespace Autoclicker;
    auto SocketPath = Utils::Paths::runtime_socket();
    for (auto Index = 1; Index + 1 < ArgumentCount; ++Index) {
        if (std::string_view{ArgumentValues[Index]} == "--socket") SocketPath = ArgumentValues[++Index];
    }
    Utils::Logger::instance().initialize("daemon", Utils::Paths::state_directory() / "daemon.log");
    auto Daemon = Core::Daemon{std::move(SocketPath)};
    if (auto Result = Daemon.run(); !Result) {
        Utils::ErrorHandler::report(Result.error());
        return 1;
    }
    return 0;
}
