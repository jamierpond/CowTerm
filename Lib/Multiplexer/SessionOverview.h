#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The phone's overview, without drawing: every session on every machine as one
// row, waiting ones first, then by most recent typing, then every project
// folder with no session open. Built from what each daemon answers to `info`
// and `activity`, and each host's project scan.
namespace term::overview
{
// One line of a daemon's `activity` answer.
struct PaneActivityReport
{
    std::string paneId;
    std::int64_t lastInputMilliseconds = 0;
    std::int64_t lastOutputMilliseconds = 0;
    std::string state;
    std::int64_t stateSinceMilliseconds = 0;
    std::string stateMessage;

    // What Jamie has not seen there (PaneReadState): finished, waiting, bell or
    // notification, empty when read. Daemons from before unread send none.
    std::string unreadEvent;
    std::int64_t unreadSinceMilliseconds = 0;
    std::string unreadText;
};

std::vector<PaneActivityReport> parseActivityReport(const std::string& payload);

// One pane as one machine's daemon describes it.
struct PaneOnHost
{
    std::string paneId;
    std::string foregroundProcess;
    std::string sessionName;
    PaneActivityReport activity;
    std::string workingDirectory;
};

// The session a pane counts as in: its own session name, else the name of the
// folder it is in. A protocol 4 daemon knows no session for the Mac's own
// panes, and a Mac session is one project folder, so its panes still group.
// Empty when the pane has neither.
std::string nameSessionOfPane(const std::string& sessionName,
                              const std::string& workingDirectory);

struct HostPanes
{
    std::string host;
    std::vector<PaneOnHost> panes;

    // The folders the host's project scan listed, as paths on the host.
    std::vector<std::string> projectFolders;
};

struct OverviewRow
{
    std::string host;
    std::string sessionName;
    std::vector<std::string> paneIds;
    bool isClaude = false;
    bool isWaiting = false;

    // "waiting: <what it asked>", "working, 4 min", "finished 12 min ago";
    // "not open · ~/projects/iac" for a project.
    std::string stateLine;
    std::int64_t lastUsedMilliseconds = 0;

    // A folder the host's scan found with no session open in it: sessionName
    // is the folder's name, and the row has no panes.
    bool isProject = false;
    std::string projectFolder;
};

// Panes with no session name are grouped by the folder they are in; a pane
// with neither is left out. Each host's project folders follow every session,
// except those a session on that host is already named after: by the name the
// Mac gives the project's session ("macprorack/iac"), or by the folder's name
// alone (a protocol 4 daemon's own panes).
std::vector<OverviewRow> buildOverviewRows(const std::vector<HostPanes>& hosts,
                                           std::int64_t nowMilliseconds);
} // namespace term::overview
