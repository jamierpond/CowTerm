#include "LocalHostName.h"

#include <windows.h>

#include <array>

namespace term
{
namespace
{
constexpr auto hostNameCapacity = std::size_t {256};
} // namespace

std::string getShortLocalHostName()
{
    auto name = std::array<char, hostNameCapacity> {};
    auto length = static_cast<DWORD>(name.size());

    if (!GetComputerNameExA(ComputerNamePhysicalDnsHostname, name.data(), &length))
        return {};

    return {name.data(), length};
}
} // namespace term
