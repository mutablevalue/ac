#include "TestHarness.hpp"

#include "Ipc/Protocol.hpp"

namespace Autoclicker::Tests {

auto protocol_tests() -> void {
    auto Configuration = Types::ConfigurationData{};
    Configuration.RateOffset = 2;
    Configuration.TargetApplication = "Minecraft";
    auto Message = Types::IpcMessage{.Command = Types::IpcCommand::SetConfiguration,
                                     .Configuration = Configuration};
    const auto Encoded = Ipc::Protocol::encode(Message);
    const auto Decoded = Ipc::Protocol::decode(Encoded);
    require(Decoded.has_value(), "encoded protocol message must decode");
    require(Decoded->Command == Types::IpcCommand::SetConfiguration, "command must round trip");
    require(Decoded->Configuration && Decoded->Configuration->Cps == 20, "configuration must round trip");
    require(Decoded->Configuration && Decoded->Configuration->RateOffset == 2,
            "rate offset must round trip");
    require(Decoded->Configuration && Decoded->Configuration->TargetApplication == "Minecraft",
            "target application must round trip");
    const auto WindowMessage = Types::IpcMessage{.Command = Types::IpcCommand::SetOwnWindow,
                                                  .WindowId = 42};
    const auto DecodedWindow = Ipc::Protocol::decode(Ipc::Protocol::encode(WindowMessage));
    require(DecodedWindow && DecodedWindow->WindowId == 42, "GUI window identity must round trip");
    require(!Ipc::Protocol::decode("not-json"), "malformed protocol payload must fail");
}

} // namespace Autoclicker::Tests
