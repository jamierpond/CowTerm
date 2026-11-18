#include "SessionOverview.h"

#include "Projects.h"
#include "RemoteLocation.h"

#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <utility>

namespace term::overview
{
namespace
{
constexpr auto millisecondsPerMinute = std::int64_t {60000};
constexpr auto activityColumnCount = 6;
constexpr auto activityColumnCountWithUnread = 9;
constexpr auto waitingState = "waiting";
constexpr auto workingState = "working";
constexpr auto finishedState = "finished";
constexpr auto notOpenProjectPrefix = "not open \xc2\xb7 ";

std::int64_t readMilliseconds(const std::string& text)
{
    return eacp::Strings::parseOr<std::int64_t>(text, 0);
}

// Whole minutes between two times, never negative.
std::int64_t calculateWholeMinutesBetween(std::int64_t earlier, std::int64_t later)
{
    return std::max<std::int64_t>(0, (later - earlier) / millisecondsPerMinute);
}

std::string describeState(const PaneActivityReport& activity, std::int64_t now)
{
    const auto minutes = std::to_string(calculateWholeMinutesBetween(activity.stateSinceMilliseconds, now));

    if (activity.state == waitingState)
        return "waiting: " + activity.stateMessage;

    if (activity.state == workingState)
        return "working, " + minutes + " min";

    if (activity.state == finishedState)
        return "finished " + minutes + " min ago";

    return {};
}

bool isClaudeProcess(const std::string& foregroundProcess)
{
    return foregroundProcess.find("claude") != std::string::npos;
}

// The pane whose state speaks for its session: a waiting one, else the one
// whose state changed last.
const PaneOnHost& findPaneThatSpeaksForSession(const std::vector<const PaneOnHost*>& panes)
{
    const auto isWaiting = [](const PaneOnHost* pane) { return pane->activity.state == waitingState; };

    if (const auto waiting = std::ranges::find_if(panes, isWaiting); waiting != panes.end())
        return **waiting;

    return **std::ranges::max_element(
        panes, {}, [](const PaneOnHost* pane) { return pane->activity.stateSinceMilliseconds; });
}

struct SessionPanes
{
    std::string host;
    std::string sessionName;
    std::vector<const PaneOnHost*> panes;
};

void addUnreadMarks(OverviewRow& row, const std::vector<const PaneOnHost*>& panes)
{
    for (const auto* pane: panes)
    {
        if (const auto mark = findUnreadMark(pane->activity))
        {
            row.unread = chooseNewerUnreadMark(row.unread, mark);
            row.unreadPaneIds.push_back(pane->paneId);
        }
    }
}

OverviewRow makeRow(const SessionPanes& session, std::int64_t now)
{
    auto row = OverviewRow {};
    row.host = session.host;
    row.sessionName = session.sessionName;

    for (const auto* pane: session.panes)
    {
        row.paneIds.push_back(pane->paneId);
        row.isClaude = row.isClaude || isClaudeProcess(pane->foregroundProcess);
        row.lastUsedMilliseconds = std::max(row.lastUsedMilliseconds, pane->activity.lastInputMilliseconds);
    }

    addUnreadMarks(row, session.panes);
    const auto& speaker = findPaneThatSpeaksForSession(session.panes);
    row.isWaiting = speaker.activity.state == waitingState;
    row.stateLine = describeState(speaker.activity, now);
    return row;
}

bool isProjectOpen(const std::string& host,
                   const std::string& folder,
                   const std::vector<OverviewRow>& sessions)
{
    const auto macSessionName = getSessionNameForProject(remote::makeRemoteProjectDir(host, folder));
    const auto folderName = sessionNameFor(folder);

    return std::ranges::any_of(sessions,
                               [&](const OverviewRow& session)
                               {
                                   const auto isNamedAfterProject = session.sessionName == macSessionName
                                                                    || session.sessionName == folderName;
                                   return session.host == host && isNamedAfterProject;
                               });
}

OverviewRow makeProjectRow(const std::string& host, const std::string& folder)
{
    auto row = OverviewRow {};
    row.host = host;
    row.sessionName = sessionNameFor(folder);
    row.stateLine = notOpenProjectPrefix + compactRemoteHomePath(folder);
    row.isProject = true;
    row.projectFolder = folder;
    return row;
}

void addProjectsWithNoSessionOpen(const std::vector<HostPanes>& hosts, std::vector<OverviewRow>& rows)
{
    const auto sessions = rows;

    for (const auto& host: hosts)
    {
        for (const auto& folder: host.projectFolders)
        {
            if (!isProjectOpen(host.host, folder, sessions))
                rows.push_back(makeProjectRow(host.host, folder));
        }
    }
}

bool shouldRowComeFirst(const OverviewRow& first, const OverviewRow& second)
{
    if (first.unread.has_value() != second.unread.has_value())
        return first.unread.has_value();

    if (first.unread && first.unread->sinceMilliseconds != second.unread->sinceMilliseconds)
        return first.unread->sinceMilliseconds > second.unread->sinceMilliseconds;

    if (first.isWaiting != second.isWaiting)
        return first.isWaiting;

    return first.lastUsedMilliseconds > second.lastUsedMilliseconds;
}
} // namespace

std::vector<PaneActivityReport> parseActivityReport(const std::string& payload)
{
    auto reports = std::vector<PaneActivityReport> {};

    for (const auto& line: eacp::Strings::split(payload, '\n'))
    {
        const auto columns = eacp::Strings::split(line, '\t');

        if (columns.size() < activityColumnCount)
            continue;

        auto report = PaneActivityReport {};
        report.paneId = columns[0];
        report.lastInputMilliseconds = readMilliseconds(columns[1]);
        report.lastOutputMilliseconds = readMilliseconds(columns[2]);
        report.state = columns[3];
        report.stateSinceMilliseconds = readMilliseconds(columns[4]);
        report.stateMessage = columns[5];

        if (columns.size() >= activityColumnCountWithUnread)
        {
            report.unreadEvent = columns[6];
            report.unreadSinceMilliseconds = readMilliseconds(columns[7]);
            report.unreadText = columns[8];
        }

        reports.push_back(report);
    }

    return reports;
}

std::optional<UnreadMark> findUnreadMark(const PaneActivityReport& activity)
{
    if (activity.unreadEvent.empty())
        return std::nullopt;

    return UnreadMark {activity.unreadEvent, activity.unreadSinceMilliseconds, activity.unreadText};
}

std::string nameSessionOfPane(const std::string& sessionName,
                              const std::string& workingDirectory)
{
    if (!sessionName.empty())
        return sessionName;

    auto folder = std::filesystem::path {workingDirectory};

    if (!folder.has_filename())
        folder = folder.parent_path();

    return folder.filename().string();
}

std::vector<OverviewRow> buildOverviewRows(const std::vector<HostPanes>& hosts,
                                           std::int64_t nowMilliseconds)
{
    auto rows = std::vector<OverviewRow> {};

    for (const auto& host: hosts)
    {
        auto panesBySession = std::map<std::string, std::vector<const PaneOnHost*>> {};

        for (const auto& pane: host.panes)
        {
            const auto sessionName =
                nameSessionOfPane(pane.sessionName, pane.workingDirectory);

            if (!sessionName.empty())
                panesBySession[sessionName].push_back(&pane);
        }

        for (const auto& [sessionName, panes]: panesBySession)
            rows.push_back(makeRow({host.host, sessionName, panes}, nowMilliseconds));
    }

    std::ranges::stable_sort(rows, shouldRowComeFirst);
    addProjectsWithNoSessionOpen(hosts, rows);
    return rows;
}
} // namespace term::overview
