#include "SessionOverviewSearch.h"

#include "PaletteRanking.h"

#include <FrecencySearch/LearnedPicks.h>

#include <algorithm>

namespace term::overview
{
namespace
{
// The phone knows only when each session was last typed in, so that counts
// as one pick.
constexpr auto usageOfTheLastTyping = 1.0;

// One row per session per machine, so the machine makes the key unique. Its
// last path component is the session name, the Mac's key for a session with
// no project directory, so the key adds nothing searchable.
std::string makeOverviewRowKey(const OverviewRow& row)
{
    return row.host + "/" + row.sessionName;
}

PaletteItem makePaletteItem(const OverviewRow& row)
{
    auto item = PaletteItem {};
    item.kind = PaletteItem::Kind::Session;
    item.key = makeOverviewRowKey(row);
    item.label = row.sessionName;
    item.status = row.stateLine;
    item.claude = row.isClaude;
    item.host = row.host;
    item.lastUsed = row.lastUsedMilliseconds;
    return item;
}

// Every key ranked comes from one of the rows.
const OverviewRow& findRowWithKey(const std::vector<OverviewRow>& rows, const std::string& key)
{
    return *std::find_if(rows.begin(),
                         rows.end(),
                         [&key](const OverviewRow& row) { return makeOverviewRowKey(row) == key; });
}

PaletteUsage makeUsageFromLastTyping(const std::vector<OverviewRow>& rows,
                                     std::int64_t nowMilliseconds)
{
    auto usage = [&rows, nowMilliseconds](const std::string& key)
    {
        const auto& row = findRowWithKey(rows, key);
        return frecencysearch::calculateDecayedUsage(
            usageOfTheLastTyping, row.lastUsedMilliseconds, nowMilliseconds);
    };

    return {usage, {}};
}
} // namespace

std::vector<OverviewRow> searchOverviewRows(const std::vector<OverviewRow>& rows,
                                            const std::string& query,
                                            std::int64_t nowMilliseconds)
{
    if (query.empty())
        return rows;

    auto items = std::vector<PaletteItem> {};

    for (const auto& row: rows)
        items.push_back(makePaletteItem(row));

    const auto ranked =
        rankPaletteItems(items, query, makeUsageFromLastTyping(rows, nowMilliseconds));
    auto matchingRows = std::vector<OverviewRow> {};

    for (const auto& item: ranked)
        matchingRows.push_back(findRowWithKey(rows, item.key));

    return matchingRows;
}
} // namespace term::overview
