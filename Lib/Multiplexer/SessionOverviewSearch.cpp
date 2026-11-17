#include "SessionOverviewSearch.h"

#include "PaletteRanking.h"
#include "Projects.h"
#include "RemoteLocation.h"

#include <FrecencySearch/LearnedPicks.h>

#include <algorithm>
#include <map>

namespace term::overview
{
namespace
{
// The phone knows only when each session was last typed in, so that counts
// as one pick.
constexpr auto usageOfTheLastTyping = 1.0;

// A project's key is its folder on its host, as the Mac's palette keys it, so
// two folders with one name on one host stay two rows.
std::string makeRankingKey(const OverviewRow& row)
{
    if (row.isProject)
        return remote::makeRemoteProjectDir(row.host, row.projectFolder);

    return makeOverviewRowKey(row);
}

PaletteItem makePaletteItem(const OverviewRow& row)
{
    auto item = PaletteItem {};
    item.kind = PaletteItem::Kind::Session;
    item.key = makeRankingKey(row);
    item.label = row.sessionName;
    item.status = row.stateLine;
    item.claude = row.isClaude;
    item.host = row.host;
    item.lastUsed = row.lastUsedMilliseconds;

    if (row.isProject)
        item.kind = PaletteItem::Kind::Project;

    return item;
}

struct RowWithUsage
{
    const OverviewRow* row = nullptr;
    double usage = 0.0;
};

std::map<std::string, RowWithUsage> calculateUsageByRankingKey(const std::vector<OverviewRow>& rows,
                                                                std::int64_t nowMilliseconds,
                                                                const SessionOpeningUsage& openingUsage)
{
    auto usageByKey = std::map<std::string, RowWithUsage> {};

    for (const auto& row: rows)
    {
        const auto typing = frecencysearch::calculateDecayedUsage(
            usageOfTheLastTyping, row.lastUsedMilliseconds, nowMilliseconds);
        usageByKey[makeRankingKey(row)] = {&row, openingUsage(makeOverviewRowKey(row)) + typing};
    }

    return usageByKey;
}

// Every row matches nothing typed: open sessions first, then projects, as in
// the Mac's palette, each by usage.
std::vector<OverviewRow> orderByUsage(const std::vector<OverviewRow>& rows,
                                      const std::map<std::string, RowWithUsage>& usageByKey)
{
    auto ordered = rows;
    const auto readUsage = [&usageByKey](const OverviewRow& row) { return usageByKey.at(makeRankingKey(row)).usage; };
    std::ranges::stable_sort(ordered,
                             [&readUsage](const OverviewRow& first, const OverviewRow& second)
                             { return readUsage(first) > readUsage(second); });
    std::ranges::stable_partition(ordered, [](const OverviewRow& row) { return !row.isProject; });
    return ordered;
}
} // namespace

// One row per session per machine, so the machine makes the key unique. Its
// last path component is the session name, the Mac's key for a session with
// no project directory, so the key adds nothing searchable. A project's key is
// the one the session it opens will have, so its openings carry over.
std::string makeOverviewRowKey(const OverviewRow& row)
{
    if (row.isProject)
        return row.host + "/" + getSessionNameForProject(remote::makeRemoteProjectDir(row.host, row.projectFolder));

    return row.host + "/" + row.sessionName;
}

std::vector<OverviewRow> searchOverviewRows(const std::vector<OverviewRow>& rows,
                                            const std::string& query,
                                            std::int64_t nowMilliseconds,
                                            const SessionOpeningUsage& openingUsage)
{
    const auto usageByKey = calculateUsageByRankingKey(rows, nowMilliseconds, openingUsage);

    if (query.empty())
        return orderByUsage(rows, usageByKey);

    auto items = std::vector<PaletteItem> {};

    for (const auto& row: rows)
        items.push_back(makePaletteItem(row));

    const auto usage = PaletteUsage {[&usageByKey](const std::string& key) { return usageByKey.at(key).usage; }, {}};
    auto matchingRows = std::vector<OverviewRow> {};

    for (const auto& item: rankPaletteItems(items, query, usage))
        matchingRows.push_back(*usageByKey.at(item.key).row);

    return matchingRows;
}
} // namespace term::overview
