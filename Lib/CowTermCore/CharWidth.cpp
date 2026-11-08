#include "TermTypes.h"

#include <algorithm>
#include <array>

namespace term
{
namespace
{
struct CodepointRange
{
    char32_t first;
    char32_t last;
};

constexpr std::array<char32_t, 4> zeroWidthCodepoints = {0x200b, 0x200c, 0x200d, 0xfeff};

constexpr std::array<CodepointRange, 6> combiningRanges = {{
    {0x0300, 0x036f},
    {0x1ab0, 0x1aff},
    {0x1dc0, 0x1dff},
    {0x20d0, 0x20ff},
    {0xfe00, 0xfe0f},
    {0xfe20, 0xfe2f},
}};

constexpr std::array<CodepointRange, 16> doubleWidthRanges = {{
    {0x1100, 0x115f},
    {0x2e80, 0x303e},
    {0x3041, 0x33ff},
    {0x3400, 0x4dbf},
    {0x4e00, 0x9fff},
    {0xa000, 0xa4cf},
    {0xac00, 0xd7a3},
    {0xf900, 0xfaff},
    {0xfe30, 0xfe4f},
    {0xff00, 0xff60},
    {0xffe0, 0xffe6},
    {0x1f300, 0x1f64f},
    {0x1f680, 0x1f6ff},
    {0x1f900, 0x1f9ff},
    {0x20000, 0x2fffd},
    {0x30000, 0x3fffd},
}};

template <std::size_t rangeCount>
bool isInAnyRange(char32_t codepoint, const std::array<CodepointRange, rangeCount>& ranges)
{
    return std::any_of(ranges.begin(),
                       ranges.end(),
                       [codepoint](const CodepointRange& range)
                       { return codepoint >= range.first && codepoint <= range.last; });
}

bool isZeroWidth(char32_t codepoint)
{
    const auto isListed = std::find(zeroWidthCodepoints.begin(), zeroWidthCodepoints.end(), codepoint)
                          != zeroWidthCodepoints.end();
    return isListed || isInAnyRange(codepoint, combiningRanges);
}
} // namespace

int charWidth(char32_t codepoint)
{
    if (isZeroWidth(codepoint))
        return 0;

    if (isInAnyRange(codepoint, doubleWidthRanges))
        return 2;

    return 1;
}
} // namespace term
