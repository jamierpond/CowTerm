#pragma once

#include <optional>
#include <string>
#include <vector>

// `ct pane-state <working|waiting|finished> [--message <text>] [--pane <id>]`:
// a program in a pane (Claude Code's hooks) tells this machine's daemon what
// it is doing, so the phone's overview can put the waiting panes first and
// the daemon can ring the phone. `ct pane-state --claude-hook` reads the
// state from the JSON Claude Code hands its hooks on stdin.
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
    bool readsClaudeHookInput = false;
};

// defaultPaneId is $COWTERM_PANE, which --pane overrides.
ParsedPaneStateReport parsePaneStateArguments(const std::vector<std::string>& args,
                                              const std::string& defaultPaneId);

// A prompt submitted is working, a permission or input question is waiting
// with the question, the end of a turn is finished with Claude's last line.
// Nullopt for any other hook event, or input that is not a hook's JSON.
std::optional<PaneStateReport> readClaudeHookReport(const std::string& hookInputJson,
                                                    const std::string& paneId);

// Hands the report to this machine's daemon and returns ct's exit code: 0 when
// the daemon took it, 2 for a usage error, 4 when no daemon is running.
int runPaneStateCommand(const std::vector<std::string>& args, const std::string& defaultPaneId);
} // namespace term::cli
