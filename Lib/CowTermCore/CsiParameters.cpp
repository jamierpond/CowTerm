#include "CsiParameters.h"

#include <algorithm>

namespace term
{
namespace
{
constexpr auto largestParameterValue = 65535;
constexpr auto decimalBase = 10;
constexpr auto typicalGroupCount = std::size_t {8};
} // namespace

void CsiParameters::clear()
{
    groups.reserve(typicalGroupCount);
    groups.clear();
    groups.emplace_back();
}

CsiParameters::Group& CsiParameters::lastGroupWithAValue()
{
    auto& group = groups.back();

    if (group.empty())
        group.push_back(0);

    return group;
}

void CsiParameters::appendDigit(int digit)
{
    auto& value = lastGroupWithAValue().back();
    value = std::min(value * decimalBase + digit, largestParameterValue);
}

void CsiParameters::startGroup()
{
    groups.emplace_back();
}

void CsiParameters::startSubParameter()
{
    lastGroupWithAValue().push_back(0);
}

int CsiParameters::value(std::size_t index, int fallback) const
{
    if (index >= groups.size() || groups[index].empty())
        return fallback;

    return groups[index][0];
}

int CsiParameters::count(std::size_t index) const
{
    return std::max(value(index, 1), 1);
}
} // namespace term
