#include <Multiplexer/SessionOverviewSearch.h>

#include <NanoTest/NanoTest.h>

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

std::vector<std::string> listHostsAndNames(const std::vector<OverviewRow>& rows)
{
    auto names = std::vector<std::string> {};

    for (const auto& row: rows)
        names.push_back(row.host + " " + row.sessionName);

    return names;
}
} // namespace

auto tEmptyQueryKeepsOrder = test("OverviewSearch/emptyTextKeepsTheOverviewsOrder") = []
{
    const auto rows = std::vector {makeRow(laptop, "cowterm", now - oneDay),
                                   makeRow(laptop, "miro", now - oneMinute)};

    check(listHostsAndNames(searchOverviewRows(rows, "", now)) == listHostsAndNames(rows),
          "waiting first, then by typing, as the overview built it");
};

auto tNameMatches = test("OverviewSearch/onlyRowsWhoseNameMatchesAreKept") = []
{
    const auto rows = std::vector {makeRow(laptop, "cowterm", now - oneMinute),
                                   makeRow(laptop, "miro", now - oneMinute),
                                   makeRow(laptop, "tamber-web", now - oneMinute)};

    check(listHostsAndNames(searchOverviewRows(rows, "cow", now))
          == std::vector<std::string> {"jamie@tamby cowterm"});
};

auto tRecentTypingFirst = test("OverviewSearch/equalMatchesPutTheMostRecentlyTypedFirst") = []
{
    const auto rows = std::vector {makeRow(laptop, "miro-old", now - oneDay),
                                   makeRow(laptop, "miro-new", now - oneMinute)};

    check(listHostsAndNames(searchOverviewRows(rows, "miro", now))
          == std::vector<std::string> {"jamie@tamby miro-new", "jamie@tamby miro-old"});
};

auto tHostAndName = test("OverviewSearch/theHostTypedWithTheNamePicksThatMachine") = []
{
    const auto rows = std::vector {makeRow(laptop, "miro", now - oneMinute),
                                   makeRow(rack, "miro", now - oneDay)};

    check(listHostsAndNames(searchOverviewRows(rows, "macprorackmiro", now))
          == std::vector<std::string> {"jamiepond@macprorack miro"});
    check(searchOverviewRows(rows, "miro", now).size() == 2,
          "the same name on two machines is two rows");
};

auto tStateLineAtWordStart = test("OverviewSearch/theStateLineMatchesOnlyWhereAWordStarts") = []
{
    const auto waitingRow = makeRow(laptop, "cowterm", now - oneMinute, "waiting: run tests?");
    const auto workingRow = makeRow(laptop, "miro", now - oneMinute, "working, 4 min");
    const auto rows = std::vector {waitingRow, workingRow};

    check(listHostsAndNames(searchOverviewRows(rows, "waiting", now))
          == std::vector<std::string> {"jamie@tamby cowterm"});
    check(searchOverviewRows(rows, "aiting", now).empty(), "not in the middle of a word");
};

auto tClaudeRows = test("OverviewSearch/claudeFindsTheRowsRunningClaude") = []
{
    auto claudeRow = makeRow(laptop, "cowterm", now - oneMinute);
    claudeRow.isClaude = true;
    const auto rows = std::vector {claudeRow, makeRow(laptop, "miro", now - oneMinute)};

    check(listHostsAndNames(searchOverviewRows(rows, "claude", now))
          == std::vector<std::string> {"jamie@tamby cowterm"});
};
