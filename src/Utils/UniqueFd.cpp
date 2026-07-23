#include "Utils/UniqueFd.hpp"

#include <utility>
#include <unistd.h>

namespace Autoclicker::Utils {

UniqueFd::UniqueFd(const int Descriptor) noexcept : DescriptorValue(Descriptor) {}
UniqueFd::~UniqueFd() { reset(); }

UniqueFd::UniqueFd(UniqueFd&& Other) noexcept : DescriptorValue(std::exchange(Other.DescriptorValue, -1)) {}

auto UniqueFd::operator=(UniqueFd&& Other) noexcept -> UniqueFd& {
    if (this != &Other) reset(std::exchange(Other.DescriptorValue, -1));
    return *this;
}

auto UniqueFd::get() const noexcept -> int { return DescriptorValue; }
UniqueFd::operator bool() const noexcept { return DescriptorValue >= 0; }
auto UniqueFd::release() noexcept -> int { return std::exchange(DescriptorValue, -1); }
auto UniqueFd::reset(const int NewDescriptor) noexcept -> void {
    if (DescriptorValue >= 0) static_cast<void>(close(DescriptorValue));
    DescriptorValue = NewDescriptor;
}

} // namespace Autoclicker::Utils
