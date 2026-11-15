#pragma once

#include <emberstore/Emberstore.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace term
{
template <typename Entry, typename RankEntry>
auto rankEntriesLowestFirst(emberstore::Collection<Entry>& collection, RankEntry rankEntry)
{
    using Rank = decltype(rankEntry(std::string {}));
    auto ranked = std::vector<std::pair<Rank, std::string>> {};

    for (auto& key: collection.ids())
        ranked.emplace_back(rankEntry(key), key);

    std::sort(ranked.begin(), ranked.end());
    return ranked;
}

template <typename Entry, typename RankEntry>
void removeLowestRankedEntriesOverLimit(emberstore::Collection<Entry>& collection,
                                        std::size_t maximumEntries,
                                        RankEntry rankEntry)
{
    if (collection.size() <= maximumEntries)
        return;

    const auto ranked = rankEntriesLowestFirst(collection, rankEntry);
    const auto excess = ranked.size() - maximumEntries;

    for (std::size_t index = 0; index < excess; ++index)
        collection.remove(ranked[index].second);
}
} // namespace term
