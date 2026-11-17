#pragma once

#include <optional>
#include <string>
#include <vector>

// `ct pane-state <working|waiting|finished> [--message <text>] [--pane <id>]`:
// a program in a pane (Claude Code's hooks) tells this machine's daemon what
// it is doing, so the phone's overview can put the waiting panes first.
namespace term::cli
{
struct PaneStateReport
{
    std::string paneId;
    std::string state;
    std::string message;
};

struct ParsedPaneStateReport
{
    std::optional<PaneStateReport> report;
    std::string usageError;
};

// defaultPaneId is $COWTERM_PANE, which --pane overrides.
ParsedPaneStateReport parsePaneStateArguments(const std::vector<std::string>& args,
                                              const std::string& defaultPaneId);

// Hands the report to this machine's daemon and returns ct's exit code: 0 when
// the daemon took it, 2 for a usage error, 4 when no daemon is running.
int runPaneStateCommand(const std::vector<std::string>& args, const std::string& defaultPaneId);
} // namespace term::cli
