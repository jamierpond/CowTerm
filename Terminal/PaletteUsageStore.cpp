#include "PaletteUsageStore.h"

#include <FrecencySearch/MatchLevel.h>

#include <algorithm>
#include <utility>

namespace term
{
namespace
{
std::string calculateLearnedPickId(const std::string& query, const std::string& destinationKey)
{
    return query + "\n" + destinationKey;
}

frecencysearch::LearnedPick toLearnedPick(const LearnedPickEntry& entry)
{
    return {entry.query, entry.destinationKey, entry.count, entry.at};
}
} // namespace

PaletteUsageStore::PaletteUsageStore(const emberstore::Database& db)
    : usage(db.collection<UsageEntry>("paletteUsage"))
    , learnedPicks(db.collection<LearnedPickEntry>("paletteLearnedPicks"))
{
}

void PaletteUsageStore::recordUse(const std::string& key, double weight, std::int64_t nowMs)
{
    usage.doc(key).set({getUsage(key, nowMs) + weight, nowMs});
    forgetLeastUsed(nowMs);
}

void PaletteUsageStore::recordPalettePick(const std::string& query,
                                          const std::string& key,
                                          std::int64_t nowMs)
{
    recordUse(key, pickWeight, nowMs);

    auto normalisedQuery = frecencysearch::normaliseQuery(query);

    if (normalisedQuery.empty())
        return;

    auto id = calculateLearnedPickId(normalisedQuery, key);
    auto previous = frecencysearch::LearnedPick {normalisedQuery, key};

    if (auto entry = learnedPicks.get(id))
        previous = toLearnedPick(*entry);

    auto updated = frecencysearch::addPickTo(previous, nowMs);
    learnedPicks.set(id, {updated.query, updated.destinationKey, updated.count, updated.atMs});
    forgetOldPicks(nowMs);
}

double PaletteUsageStore::getUsage(const std::string& key, std::int64_t nowMs)
{
    if (auto entry = usage.get(key))
        return frecencysearch::calculateDecayedUsage(entry->count, entry->at, nowMs);

    return 0;
}

std::vector<frecencysearch::LearnedPick>
    PaletteUsageStore::findPicksForQuery(const std::string& query, std::int64_t nowMs)
{
    auto normalisedQuery = frecencysearch::normaliseQuery(query);
    return frecencysearch::findLearnedPicksForQuery(getAllLearnedPicks(), normalisedQuery, nowMs);
}

std::vector<frecencysearch::LearnedPick> PaletteUsageStore::getAllLearnedPicks()
{
    auto picks = std::vector<frecencysearch::LearnedPick> {};

    for (auto& entry: learnedPicks.values())
        picks.push_back(toLearnedPick(entry));

    return picks;
}

void PaletteUsageStore::forgetOldPicks(std::int64_t nowMs)
{
    for (auto& pick: frecencysearch::findLearnedPicksToForget(getAllLearnedPicks(), nowMs))
        learnedPicks.remove(calculateLearnedPickId(pick.query, pick.destinationKey));
}

void PaletteUsageStore::forgetLeastUsed(std::int64_t nowMs)
{
    if (usage.size() <= maximumUsageEntries)
        return;

    auto ranked = std::vector<std::pair<double, std::string>> {};

    for (auto& key: usage.ids())
        ranked.push_back({getUsage(key, nowMs), key});

    std::sort(ranked.begin(), ranked.end());

    auto excess = ranked.size() - maximumUsageEntries;

    for (std::size_t index = 0; index < excess; ++index)
        usage.remove(ranked[index].second);
}
} // namespace term
