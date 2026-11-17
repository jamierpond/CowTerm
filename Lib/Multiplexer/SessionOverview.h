#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The phone's overview, without drawing: every session on every machine as one
// row, waiting ones first, then by most recent typing. Built from what each
// daemon answers to `info` and `activity`.
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
};

struct OverviewRow
{
    std::string host;
    std::string sessionName;
    std::vector<std::string> paneIds;
    bool isClaude = false;
    bool isWaiting = false;

    // "waiting: <what it asked>", "working, 4 min", "finished 12 min ago".
    std::string stateLine;
    std::int64_t lastUsedMilliseconds = 0;
};

// Panes with no session name are grouped by the folder they are in; a pane
// with neither is left out.
std::vector<OverviewRow> buildOverviewRows(const std::vector<HostPanes>& hosts,
                                           std::int64_t nowMilliseconds);
} // namespace term::overview
