#pragma once

#include "Types/ConfigurationTypes.hpp"
#include "Utils/Error.hpp"

#include <expected>
#include <filesystem>
#include <shared_mutex>
#include <string>

namespace Autoclicker::Core {

class Configuration final {
public:
    static auto instance() -> Configuration&;

    Configuration(const Configuration&) = delete;
    auto operator=(const Configuration&) -> Configuration& = delete;

    [[nodiscard]] auto snapshot() const -> Types::ConfigurationData;
    auto update(Types::ConfigurationData Value) -> std::expected<void, Utils::Error>;
    auto load(const std::filesystem::path& Path) -> std::expected<void, Utils::Error>;
    auto save(const std::filesystem::path& Path) const -> std::expected<void, Utils::Error>;
    [[nodiscard]] static auto validate(const Types::ConfigurationData& Value)
        -> std::expected<void, Utils::Error>;

private:
    Configuration() = default;

    mutable std::shared_mutex Mutex;
    Types::ConfigurationData Data;
};

[[nodiscard]] auto key_chord_name(const Types::KeyChord& Value) -> std::string;

} // namespace Autoclicker::Core
