#include "GraphemeBreak.h"

#include "TermTypes.h"

#include <algorithm>

namespace term
{
namespace
{
constexpr auto zeroWidthJoiner = char32_t {0x200d};
constexpr auto firstSkinTone = char32_t {0x1f3fb};
constexpr auto lastSkinTone = char32_t {0x1f3ff};
constexpr auto firstRegionalIndicator = char32_t {0x1f1e6};
constexpr auto lastRegionalIndicator = char32_t {0x1f1ff};
constexpr auto firstTag = char32_t {0xe0020};
constexpr auto lastTag = char32_t {0xe007f};

bool isInRange(char32_t codepoint, char32_t first, char32_t last)
{
    return codepoint >= first && codepoint <= last;
}

bool isRegionalIndicator(char32_t codepoint)
{
    return isInRange(codepoint, firstRegionalIndicator, lastRegionalIndicator);
}

bool isLoneRegionalIndicator(std::u32string_view cluster)
{
    return cluster.size() == 1 && isRegionalIndicator(cluster.front());
}
} // namespace

bool isClusterExtender(char32_t codepoint)
{
    constexpr auto firstCombiningCodepoint = char32_t {0x0300};

    if (codepoint < firstCombiningCodepoint)
        return false;

    return charWidth(codepoint) == 0 || isInRange(codepoint, firstSkinTone, lastSkinTone)
           || isInRange(codepoint, firstTag, lastTag) || isRegionalIndicator(codepoint);
}

bool doesExtendCluster(std::u32string_view cluster, char32_t next)
{
    if (cluster.empty() || cluster.front() == U' ')
        return false;

    constexpr auto emojiWidth = 2;

    if (cluster.back() == zeroWidthJoiner)
        return charWidth(next) == emojiWidth || isClusterExtender(next);

    if (isRegionalIndicator(next))
        return isLoneRegionalIndicator(cluster);

    return isClusterExtender(next);
}
} // namespace term
