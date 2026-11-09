#include "HyperlinkTable.h"

#include <algorithm>
#include <array>

namespace term
{
namespace
{
constexpr auto largestHyperlinkCount = std::size_t {4096};
constexpr auto openableSchemes =
    std::array<std::string_view, 4> {"http://", "https://", "mailto:", "file://"};
} // namespace

bool hasOpenableScheme(std::string_view uri)
{
    return std::any_of(openableSchemes.begin(),
                       openableSchemes.end(),
                       [uri](std::string_view scheme) { return uri.starts_with(scheme); });
}

HyperlinkId HyperlinkTable::intern(std::string_view uri)
{
    auto key = std::string {uri};

    if (const auto found = idsByUri.find(key); found != idsByUri.end())
        return found->second;

    if (isFull())
        return noHyperlink;

    uris.push_back(key);
    const auto id = (HyperlinkId) uris.size();
    idsByUri.emplace(std::move(key), id);
    return id;
}

std::string_view HyperlinkTable::findUri(HyperlinkId id) const
{
    if (id == noHyperlink || id > uris.size())
        return {};

    return uris[id - 1];
}

bool HyperlinkTable::isFull() const
{
    return uris.size() >= largestHyperlinkCount;
}

void HyperlinkTable::clear()
{
    uris.clear();
    idsByUri.clear();
}

std::vector<HyperlinkId> HyperlinkTable::keepOnly(const std::vector<bool>& isUsed)
{
    auto newIds = std::vector<HyperlinkId>(uris.size() + 1, noHyperlink);
    auto kept = std::move(uris);
    clear();

    for (std::size_t index = 0; index < kept.size(); ++index)
    {
        const auto oldId = index + 1;

        if (oldId < isUsed.size() && isUsed[oldId])
            newIds[oldId] = intern(kept[index]);
    }

    return newIds;
}
} // namespace term
