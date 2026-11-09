#include "CsiParameters.h"

#include <algorithm>

namespace term
{
namespace
{
constexpr auto largestParameterValue = 65535;
constexpr auto decimalBase = 10;
constexpr auto typicalGroupCount = std::size_t {8};
constexpr auto largestNumberCount = std::size_t {24};
} // namespace

void CsiParameters::clear()
{
    groups.reserve(typicalGroupCount);
    groups.clear();
    groups.emplace_back();
    numberCount = 1;
    tooManyNumbers = false;
}

bool CsiParameters::canAddNumber()
{
    if (numberCount >= largestNumberCount)
        tooManyNumbers = true;
    else
        ++numberCount;

    return !tooManyNumbers;
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
    if (tooManyNumbers)
        return;

    auto& value = lastGroupWithAValue().back();
    value = std::min(value * decimalBase + digit, largestParameterValue);
}

void CsiParameters::startGroup()
{
    if (canAddNumber())
        groups.emplace_back();
}

void CsiParameters::startSubParameter()
{
    if (!canAddNumber())
        return;

    lastGroupWithAValue().push_back(0);
}

void CsiParameters::restoreCollected(Collected saved)
{
    groups = std::move(saved.groups);
    numberCount = std::clamp(saved.numberCount, std::size_t {1}, largestNumberCount);
    tooManyNumbers = saved.tooManyNumbers;
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
