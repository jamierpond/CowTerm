#include <Multiplexer/SessionOverview.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term::overview;

namespace
{
constexpr auto now = std::int64_t {1'800'000'000'000};
constexpr auto oneMinute = std::int64_t {60'000};
constexpr auto fourMinutes = 4 * oneMinute;
constexpr auto twelveMinutes = 12 * oneMinute;

PaneOnHost makePane(const std::string& id,
                    const std::string& session,
                    const std::string& process,
                    const PaneActivityReport& activity)
{
    auto pane = PaneOnHost {};
    pane.paneId = id;
    pane.sessionName = session;
    pane.foregroundProcess = process;
    pane.activity = activity;
    pane.activity.paneId = id;
    return pane;
}

PaneActivityReport makeActivity(std::int64_t lastInput, const std::string& state, std::int64_t stateSince)
{
    auto activity = PaneActivityReport {};
    activity.lastInputMilliseconds = lastInput;
    activity.state = state;
    activity.stateSinceMilliseconds = stateSince;
    return activity;
}
} // namespace

auto tActivityReportIsRead = test("SessionOverview/activityReportIsRead") = []
{
    const auto reports = parseActivityReport("a\t10\t20\twaiting\t30\tneeds you\nbroken line\nb\t0\t0\t\t0\t\n");

    check(reports.size() == 2);

    if (reports.size() == 2)
    {
        check(reports[0].paneId == "a" && reports[0].lastInputMilliseconds == 10);
        check(reports[0].lastOutputMilliseconds == 20 && reports[0].stateSinceMilliseconds == 30);
        check(reports[0].state == "waiting" && reports[0].stateMessage == "needs you");
        check(reports[1].paneId == "b" && reports[1].state.empty());
    }
};

auto tWaitingSessionsComeFirst = test("SessionOverview/waitingSessionsComeFirst") = []
{
    auto waiting = makeActivity(now - twelveMinutes, "waiting", now - oneMinute);
    waiting.stateMessage = "Claude needs your permission to use Bash";
    const auto recent = makeActivity(now - oneMinute, "working", now - fourMinutes);

    const auto rows = buildOverviewRows(
        {{"tamby", {makePane("p1", "Grazer", "claude", recent)}},
         {"macprorack", {makePane("p2", "tamber-web", "claude", waiting)}}},
        now);

    check(rows.size() == 2);

    if (rows.size() == 2)
    {
        check(rows[0].sessionName == "tamber-web" && rows[0].isWaiting);
        check(rows[0].stateLine == "waiting: Claude needs your permission to use Bash");
        check(rows[1].sessionName == "Grazer" && rows[1].stateLine == "working, 4 min");
    }
};

auto tRecentlyUsedSessionsComeNext = test("SessionOverview/recentlyUsedSessionsComeNext") = []
{
    const auto older = makeActivity(now - twelveMinutes, "finished", now - twelveMinutes);
    const auto newer = makeActivity(now - oneMinute, {}, 0);

    const auto rows = buildOverviewRows(
        {{"tamby", {makePane("p1", "old", "zsh", older), makePane("p2", "new", "zsh", newer)}}}, now);

    check(rows.size() == 2 && rows[0].sessionName == "new");
    check(rows.size() == 2 && rows[1].stateLine == "finished 12 min ago");
};

auto tPanesOfASessionMakeOneRow = test("SessionOverview/panesOfASessionMakeOneRow") = []
{
    const auto shell = makeActivity(now - fourMinutes, {}, 0);
    const auto claude = makeActivity(now - oneMinute, "working", now - oneMinute);

    const auto rows = buildOverviewRows(
        {{"tamby", {makePane("p1", "Grazer", "zsh", shell), makePane("p2", "Grazer", "claude", claude)}}},
        now);

    check(rows.size() == 1);

    if (rows.size() == 1)
    {
        check(rows[0].paneIds.size() == 2);
        check(rows[0].isClaude);
        check(rows[0].lastUsedMilliseconds == now - oneMinute);
        check(rows[0].stateLine == "working, 1 min");
    }
};

auto tPanesWithoutASessionGroupByFolder =
    test("SessionOverview/panesWithoutASessionGroupByFolder") = []
{
    auto shell = makePane("p1", "", "zsh", {});
    shell.workingDirectory = "/Users/jamiepond/projects/CowTerm";
    auto claude = makePane("p2", "", "claude", {});
    claude.workingDirectory = "/Users/jamiepond/projects/CowTerm/";
    const auto nowhere = makePane("p3", "", "zsh", {});

    const auto rows =
        buildOverviewRows({{"big-cow", {shell, claude, nowhere}}}, now);

    check(rows.size() == 1 && rows[0].sessionName == "CowTerm");
    check(rows.size() == 1 && rows[0].paneIds.size() == 2,
          "a pane with no session and no folder is left out");
};

auto tSessionNameWinsOverFolder =
    test("SessionOverview/sessionNameWinsOverFolder") = []
{
    check(nameSessionOfPane("Grazer", "/Users/jamiepond/projects/CowTerm")
          == "Grazer");
    check(nameSessionOfPane("", "/").empty());
};
