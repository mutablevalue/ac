#pragma once

namespace Autoclicker::Utils {

class UniqueFd final {
public:
    UniqueFd() = default;
    explicit UniqueFd(int Descriptor) noexcept;
    ~UniqueFd();

    UniqueFd(const UniqueFd&) = delete;
    auto operator=(const UniqueFd&) -> UniqueFd& = delete;
    UniqueFd(UniqueFd&& Other) noexcept;
    auto operator=(UniqueFd&& Other) noexcept -> UniqueFd&;

    [[nodiscard]] auto get() const noexcept -> int;
    [[nodiscard]] explicit operator bool() const noexcept;
    auto release() noexcept -> int;
    auto reset(int Descriptor = -1) noexcept -> void;

private:
    int DescriptorValue{-1};
};

} // namespace Autoclicker::Utils
