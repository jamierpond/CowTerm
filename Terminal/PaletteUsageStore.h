#pragma once

#include <CowTermPalette/PaletteUsageEntries.h>
#include <FrecencySearch/LearnedPicks.h>
#include <Miro/Reflect.h>
#include <emberstore/Emberstore.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace term
{
// How much each session or project is used (decayed: a palette pick adds 1,
// any other switch to it seenWeight) and which one the user picked for each
// typed text. Give it an Atomic database: losing the last uses is harmless.
struct PaletteUsageStore
{
    explicit PaletteUsageStore(const emberstore::Database& db);

    static constexpr double pickWeight = 1.0;
    static constexpr double seenWeight = 0.25;
    static constexpr std::size_t maximumUsageEntries = 500;

    void recordUse(const std::string& key, double weight, std::int64_t nowMs);
    void recordPalettePick(const std::string& query, const std::string& key, std::int64_t nowMs);

    double getUsage(const std::string& key, std::int64_t nowMs);

    // Every stored use count, not decayed, by key; and every learned pick.
    std::map<std::string, UsageEntry> readAllUsage();
    std::vector<LearnedPickEntry> readAllLearnedPicks();
    std::vector<frecencysearch::LearnedPick> findPicksForQuery(const std::string& query,
                                                               std::int64_t nowMs);

    emberstore::Collection<UsageEntry> usage;
    emberstore::Collection<LearnedPickEntry> learnedPicks;

private:
    std::vector<frecencysearch::LearnedPick> getAllLearnedPicks();
    void forgetOldPicks(std::int64_t nowMs);
    void forgetLeastUsed(std::int64_t nowMs);
};
} // namespace term
