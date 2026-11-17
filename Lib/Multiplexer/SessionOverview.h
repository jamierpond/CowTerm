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
};

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

// Panes without a session name (shells opened before sessions were known to
// the daemon, popups) are left out.
std::vector<OverviewRow> buildOverviewRows(const std::vector<HostPanes>& hosts,
                                           std::int64_t nowMilliseconds);
} // namespace term::overview
