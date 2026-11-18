#include <Multiplexer/SessionOverviewSearch.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <filesystem>

// The phone overview's search: the Mac palette's match rules and order over
// the overview's rows.

using namespace nano;
using namespace term::overview;

namespace
{
constexpr auto now = std::int64_t {1'800'000'000'000};
constexpr auto oneMinute = std::int64_t {60'000};
constexpr auto oneDay = 24 * 60 * oneMinute;
constexpr auto laptop = "jamie@tamby";
constexpr auto rack = "jamiepond@macprorack";

OverviewRow makeRow(const std::string& host,
                    const std::string& sessionName,
                    std::int64_t lastUsed,
                    const std::string& stateLine = {})
{
    auto row = OverviewRow {};
    row.host = host;
    row.sessionName = sessionName;
    row.lastUsedMilliseconds = lastUsed;
    row.stateLine = stateLine;
    return row;
}

double noOpenings(const std::string&)
{
    return 0.0;
}

std::vector<std::string> listHostsAndNames(const std::vector<OverviewRow>& rows)
{
    auto names = std::vector<std::string> {};

    for (const auto& row: rows)
        names.push_back(row.host + " " + row.sessionName);

    return names;
}
} // namespace

auto tEmptyQueryRanksByTyping = test("OverviewSearch/withNoOpeningsEmptyTextRanksByLastTyping") = []
{
    const auto rows = std::vector {makeRow(laptop, "cowterm", now - oneDay),
                                   makeRow(laptop, "miro", now - oneMinute)};

    check(listHostsAndNames(searchOverviewRows(rows, "", now, noOpenings))
          == std::vector<std::string> {"jamie@tamby miro", "jamie@tamby cowterm"});
};

auto tEmptyQueryRanksByOpenings = test("OverviewSearch/emptyTextRanksBySessionsOpenedOnThePhone") = []
{
    constexpr auto opensOfCowterm = 5.0;
    const auto rows = std::vector {makeRow(laptop, "miro", now - oneMinute),
                                   makeRow(laptop, "cowterm", now - oneDay),
                                   makeRow(rack, "Grazer", 0)};
    const auto openings = [](const std::string& key)
    {
        if (key == std::string {laptop} + "/cowterm")
            return opensOfCowterm;

        return 0.0;
    };

    check(listHostsAndNames(searchOverviewRows(rows, "", now, openings))
          == std::vector<std::string> {"jamie@tamby cowterm", "jamie@tamby miro", "jamiepond@macprorack Grazer"});
};

auto tNoUsageKeepsTheOverviewsOrder = test("OverviewSearch/rowsNeverUsedKeepTheOverviewsOrder") = []
{
    const auto rows = std::vector {makeRow(rack, "Miro", 0), makeRow(laptop, "cowterm", 0), makeRow(laptop, "Grazer", 0)};

    check(listHostsAndNames(searchOverviewRows(rows, "", now, noOpenings)) == listHostsAndNames(rows));
};

auto tNameMatches = test("OverviewSearch/onlyRowsWhoseNameMatchesAreKept") = []
{
    const auto rows = std::vector {makeRow(laptop, "cowterm", now - oneMinute),
                                   makeRow(laptop, "miro", now - oneMinute),
                                   makeRow(laptop, "tamber-web", now - oneMinute)};

    check(listHostsAndNames(searchOverviewRows(rows, "cow", now, noOpenings))
          == std::vector<std::string> {"jamie@tamby cowterm"});
};

auto tRecentTypingFirst = test("OverviewSearch/equalMatchesPutTheMostRecentlyTypedFirst") = []
{
    const auto rows = std::vector {makeRow(laptop, "miro-old", now - oneDay),
                                   makeRow(laptop, "miro-new", now - oneMinute)};

    check(listHostsAndNames(searchOverviewRows(rows, "miro", now, noOpenings))
          == std::vector<std::string> {"jamie@tamby miro-new", "jamie@tamby miro-old"});
};

auto tHostAndName = test("OverviewSearch/theHostTypedWithTheNamePicksThatMachine") = []
{
    const auto rows = std::vector {makeRow(laptop, "miro", now - oneMinute),
                                   makeRow(rack, "miro", now - oneDay)};

    check(listHostsAndNames(searchOverviewRows(rows, "macprorackmiro", now, noOpenings))
          == std::vector<std::string> {"jamiepond@macprorack miro"});
    check(searchOverviewRows(rows, "miro", now, noOpenings).size() == 2,
          "the same name on two machines is two rows");
};

auto tStateLineAtWordStart = test("OverviewSearch/theStateLineMatchesOnlyWhereAWordStarts") = []
{
    const auto waitingRow = makeRow(laptop, "cowterm", now - oneMinute, "waiting: run tests?");
    const auto workingRow = makeRow(laptop, "miro", now - oneMinute, "working, 4 min");
    const auto rows = std::vector {waitingRow, workingRow};

    check(listHostsAndNames(searchOverviewRows(rows, "waiting", now, noOpenings))
          == std::vector<std::string> {"jamie@tamby cowterm"});
    check(searchOverviewRows(rows, "aiting", now, noOpenings).empty(), "not in the middle of a word");
};

auto tClaudeRows = test("OverviewSearch/claudeFindsTheRowsRunningClaude") = []
{
    auto claudeRow = makeRow(laptop, "cowterm", now - oneMinute);
    claudeRow.isClaude = true;
    const auto rows = std::vector {claudeRow, makeRow(laptop, "miro", now - oneMinute)};

    check(listHostsAndNames(searchOverviewRows(rows, "claude", now, noOpenings))
          == std::vector<std::string> {"jamie@tamby cowterm"});
};

namespace
{
constexpr auto iacFolder = "/home/jamiepond/projects/iac";
constexpr auto iacInHomeFolder = "/home/jamiepond/iac";
constexpr auto notesFolder = "/home/jamiepond/projects/notes";

OverviewRow makeProjectRow(const std::string& host, const std::string& folder)
{
    auto row = OverviewRow {};
    row.host = host;
    row.sessionName = std::filesystem::path {folder}.filename().string();
    row.isProject = true;
    row.projectFolder = folder;
    return row;
}
} // namespace

auto tSearchFindsSessionsAndProjects = test("OverviewSearch/searchFindsSessionsAndProjectsAlike") = []
{
    const auto rows = std::vector {makeRow(laptop, "iac-review", now - oneMinute),
                                   makeRow(laptop, "miro", now - oneMinute),
                                   makeProjectRow(rack, iacFolder)};

    auto found = listHostsAndNames(searchOverviewRows(rows, "iac", now, noOpenings));
    std::ranges::sort(found);

    check(found == std::vector<std::string> {"jamie@tamby iac-review", "jamiepond@macprorack iac"});
};

auto tEmptyQueryPutsProjectsAfterSessions = test("OverviewSearch/emptyTextPutsProjectsAfterEverySession") = []
{
    constexpr auto opensOfIac = 5.0;
    const auto rows = std::vector {makeProjectRow(rack, notesFolder),
                                   makeRow(laptop, "miro", now - oneDay),
                                   makeProjectRow(rack, iacFolder)};
    const auto openings = [](const std::string& key)
    {
        if (key == std::string {rack} + "/macprorack/iac")
            return opensOfIac;

        return 0.0;
    };

    const auto ordered = searchOverviewRows(rows, "", now, openings);

    check(ordered.size() == rows.size());

    if (ordered.size() == rows.size())
    {
        check(!ordered[0].isProject);
        check(ordered[1].projectFolder == iacFolder, "openings order the projects too");
        check(ordered[2].projectFolder == notesFolder);
    }
};

auto tProjectKeyIsItsSessionsKey = test("OverviewSearch/aProjectsOpeningsCountForTheSessionItOpens") = []
{
    check(makeOverviewRowKey(makeProjectRow(rack, iacFolder)) == "jamiepond@macprorack/macprorack/iac");
};

auto tTwoFoldersWithOneNameStayTwoRows = test("OverviewSearch/twoProjectFoldersWithOneNameStayTwoRows") = []
{
    const auto rows = std::vector {makeProjectRow(rack, iacFolder), makeProjectRow(rack, iacInHomeFolder)};
    const auto found = searchOverviewRows(rows, "iac", now, noOpenings);

    check(found.size() == rows.size());

    if (found.size() == rows.size())
        check(found[0].projectFolder != found[1].projectFolder);
};

auto tEmptyQueryPutsUnreadFirst = test("OverviewSearch/emptyTextPutsUnreadSessionsFirstNewestFirst") = []
{
    auto older = makeRow(laptop, "older", now - oneDay);
    older.unread = term::UnreadMark {"finished", now - oneDay, {}};
    auto newer = makeRow(rack, "newer", now - oneDay);
    newer.unread = term::UnreadMark {"waiting", now - oneMinute, {}};
    const auto rows = std::vector {makeRow(laptop, "recent", now - oneMinute), older, newer};

    check(listHostsAndNames(searchOverviewRows(rows, "", now, noOpenings))
          == std::vector<std::string> {"jamiepond@macprorack newer", "jamie@tamby older", "jamie@tamby recent"});
};

auto tTypedQueryIgnoresUnread = test("OverviewSearch/typedTextRanksByTheMatchWhetherUnreadOrNot") = []
{
    auto unread = makeRow(laptop, "grazer-docs", now - oneDay);
    unread.unread = term::UnreadMark {"finished", now - oneMinute, {}};
    const auto rows = std::vector {unread, makeRow(laptop, "grazer", now - oneMinute)};

    const auto found = searchOverviewRows(rows, "grazer", now, noOpenings);

    check(!found.empty() && found.front().sessionName == "grazer");
};
