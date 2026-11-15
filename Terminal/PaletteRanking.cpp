#include "PaletteRanking.h"

#include "RemoteLocation.h"

#include <FrecencySearch/RowOrder.h>

#include <algorithm>

namespace term
{
namespace
{
std::string getLastPathComponent(std::string path)
{
    while (path.size() > 1 && path.back() == '/')
        path.pop_back();

    auto slash = path.find_last_of('/');

    if (slash == std::string::npos)
        return path;

    return path.substr(slash + 1);
}

frecencysearch::SearchableTexts getSearchableTexts(const PaletteItem& item)
{
    auto texts = frecencysearch::SearchableTexts {};
    texts.names = {item.label, getLastPathComponent(item.key)};
    texts.details = {item.detail, item.status};

    // A remote row also answers to its host typed before or after the name,
    // as one run of letters: "macprorackmiro" or "miromacprorack".
    if (!item.host.empty())
    {
        const auto host = remote::getHostLabel(item.host);
        const auto name = getLastPathComponent(item.label);
        texts.names.push_back(host + name);
        texts.names.push_back(name + host);
    }

    if (item.claude)
        texts.details.push_back("claude");

    return texts;
}

frecencysearch::RowRankingFacts calculateRankingFacts(const PaletteItem& item,
                                                      frecencysearch::MatchLevel level,
                                                      const PaletteUsage& usage)
{
    auto facts = frecencysearch::RowRankingFacts {};
    facts.level = level;
    facts.isOpen = item.kind == PaletteItem::Kind::Session;

    if (usage.usage)
        facts.usage = usage.usage(item.key);

    for (auto& pick: usage.learnedPicks)
    {
        if (pick.destinationKey != item.key)
            continue;

        facts.learnedPicks = pick.count;
        facts.learnedAtMs = pick.atMs;
    }

    return facts;
}
} // namespace

std::vector<PaletteItem> rankPaletteItems(const std::vector<PaletteItem>& items,
                                          const std::string& query,
                                          const PaletteUsage& usage)
{
    struct RankedItem
    {
        frecencysearch::RowRankingFacts facts;
        const PaletteItem* item = nullptr;
    };

    auto normalisedQuery = frecencysearch::normaliseQuery(query);
    auto rankedItems = std::vector<RankedItem> {};

    for (auto& item: items)
        if (auto level = frecencysearch::findMatchLevel(normalisedQuery, getSearchableTexts(item)))
            rankedItems.push_back({calculateRankingFacts(item, *level, usage), &item});

    std::stable_sort(rankedItems.begin(),
                     rankedItems.end(),
                     [](const RankedItem& first, const RankedItem& second)
                     { return frecencysearch::shouldRankBefore(first.facts, second.facts); });

    auto ranked = std::vector<PaletteItem> {};

    for (auto& rankedItem: rankedItems)
        ranked.push_back(*rankedItem.item);

    return ranked;
}
} // namespace term
