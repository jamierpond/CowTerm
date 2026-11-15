#pragma once

#include <FrecencySearch/LearnedPicks.h>
#include <Miro/Reflect.h>
#include <emberstore/Emberstore.h>

#include <cstdint>
#include <string>
#include <vector>

namespace term
{
struct UsageEntry
{
    double count = 0;
    std::int64_t at = 0;

    MIRO_REFLECT(count, at)
};

struct LearnedPickEntry
{
    std::string query;
    std::string destinationKey;
    double count = 0;
    std::int64_t at = 0;

    MIRO_REFLECT(query, destinationKey, count, at)
};

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
