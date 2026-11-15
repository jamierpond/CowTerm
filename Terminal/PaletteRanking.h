#pragma once

#include "PaletteItem.h"

#include <FrecencySearch/LearnedPicks.h>

#include <functional>
#include <string>
#include <vector>

namespace term
{
// What orders a typed query's rows besides the match: each item's decayed
// usage by its key, and the picks learned for the typed text.
struct PaletteUsage
{
    std::function<double(const std::string& key)> usage;
    std::vector<frecencysearch::LearnedPick> learnedPicks;
};

// The items `query` matches, in frecencysearch::shouldRankBefore order: a
// session's or project's name ranks it, its path and status only where a
// word starts with the query. Ties keep `items`' order.
std::vector<PaletteItem> rankPaletteItems(const std::vector<PaletteItem>& items,
                                          const std::string& query,
                                          const PaletteUsage& usage);
} // namespace term
