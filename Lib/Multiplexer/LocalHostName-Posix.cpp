#include "LocalHostName.h"

#include <unistd.h>

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

    if (gethostname(name.data(), name.size() - 1) != 0)
        return {};

    const auto fullName = std::string {name.data()};
    return fullName.substr(0, fullName.find('.'));
}
} // namespace term
