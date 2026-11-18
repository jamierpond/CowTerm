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

namespace
{
constexpr auto rack = "jamiepond@macprorack";
constexpr auto laptop = "jamie@tamby";
constexpr auto iacFolder = "/home/jamiepond/projects/iac";
constexpr auto grazerFolder = "/home/jamiepond/projects/Grazer";
constexpr auto cowtermFolder = "/home/jamiepond/projects/CowTerm";

std::vector<std::string> listNamesAndKinds(const std::vector<OverviewRow>& rows)
{
    auto names = std::vector<std::string> {};

    for (const auto& row: rows)
    {
        const auto kind = std::string {row.isProject ? "project " : "session "};
        names.push_back(kind + row.sessionName);
    }

    return names;
}
} // namespace

auto tProjectsFollowEverySession = test("SessionOverview/projectsWithNoSessionFollowEverySession") = []
{
    auto host = HostPanes {rack, {makePane("pane-a", "macprorack/Grazer", "zsh", {})}, {iacFolder, grazerFolder}};
    auto other = HostPanes {laptop, {makePane("pane-b", "notes", "zsh", {})}, {}};

    const auto rows = buildOverviewRows({host, other}, now);

    check(listNamesAndKinds(rows)
          == std::vector<std::string> {"session macprorack/Grazer", "session notes", "project iac"});
};

auto tProjectRowSaysItIsNotOpen = test("SessionOverview/projectRowSaysItIsNotOpenAndWhere") = []
{
    const auto rows = buildOverviewRows({HostPanes {rack, {}, {iacFolder}}}, now);

    check(rows.size() == 1);

    if (rows.size() == 1)
    {
        check(rows[0].isProject && rows[0].projectFolder == iacFolder);
        check(rows[0].paneIds.empty());
        check(rows[0].stateLine == "not open \xc2\xb7 ~/projects/iac");
    }
};

auto tProjectOpenByFolderNameIsLeftOut = test("SessionOverview/projectWhoseFolderNamesASessionIsLeftOut") = []
{
    auto pane = makePane("pane-a", {}, "zsh", {});
    pane.workingDirectory = cowtermFolder;

    const auto rows = buildOverviewRows({HostPanes {rack, {pane}, {cowtermFolder}}}, now);

    check(listNamesAndKinds(rows) == std::vector<std::string> {"session CowTerm"},
          "a protocol 4 daemon's own panes are named after their folder");
};

auto tSessionOnAnotherHostDoesNotHideProject = test("SessionOverview/sessionOnAnotherHostDoesNotHideAProject") = []
{
    auto host = HostPanes {rack, {}, {iacFolder}};
    auto other = HostPanes {laptop, {makePane("pane-b", "macprorack/iac", "zsh", {})}, {}};

    const auto rows = buildOverviewRows({host, other}, now);

    check(listNamesAndKinds(rows) == std::vector<std::string> {"session macprorack/iac", "project iac"});
};

auto tUnreadSessionsComeBeforeWaitingOnes = test("SessionOverview/unreadSessionsComeFirstNewestFirstThenWaitingOnes") = []
{
    auto waiting = makeActivity(now - oneMinute, "waiting", now - oneMinute);
    auto olderUnread = makeActivity(now - twelveMinutes, "finished", now - twelveMinutes);
    olderUnread.unreadEvent = "finished";
    olderUnread.unreadSinceMilliseconds = now - twelveMinutes;
    auto newerUnread = makeActivity(now - twelveMinutes, "finished", now - fourMinutes);
    newerUnread.unreadEvent = "finished";
    newerUnread.unreadSinceMilliseconds = now - fourMinutes;
    newerUnread.unreadText = "All tests pass.";

    const auto rows = buildOverviewRows({{"tamby",
                                          {makePane("p1", "asks", "claude", waiting),
                                           makePane("p2", "older", "claude", olderUnread),
                                           makePane("p3", "newer", "claude", newerUnread)}}},
                                        now);

    check(rows.size() == 3);

    if (rows.size() == 3)
    {
        check(rows[0].sessionName == "newer" && rows[0].unread.has_value());
        check(rows[0].unread->text == "All tests pass.");
        check(rows[0].unreadPaneIds == std::vector<std::string> {"p3"});
        check(rows[1].sessionName == "older");
        check(rows[2].sessionName == "asks" && !rows[2].unread.has_value());
    }
};

auto tSessionTakesItsNewestUnreadPane = test("SessionOverview/sessionShowsItsNewestUnreadPaneAndListsEveryUnreadPane") = []
{
    auto bell = makeActivity(0, {}, 0);
    bell.unreadEvent = "bell";
    bell.unreadSinceMilliseconds = now - oneMinute;
    auto finished = makeActivity(0, "finished", 0);
    finished.unreadEvent = "finished";
    finished.unreadSinceMilliseconds = now - fourMinutes;

    const auto rows = buildOverviewRows(
        {{"tamby", {makePane("p1", "Grazer", "zsh", bell), makePane("p2", "Grazer", "claude", finished)}}}, now);

    check(rows.size() == 1 && rows[0].unread.has_value() && rows[0].unread->event == "bell");
    check(rows.size() == 1 && rows[0].unreadPaneIds == std::vector<std::string> {"p1", "p2"});
};
