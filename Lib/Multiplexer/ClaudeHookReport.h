#pragma once

#include "PaneStateCommand.h"

#include <optional>
#include <string>

// What `ct pane-state --claude-hook` reads from the JSON Claude Code gives its hooks on stdin.
namespace term::cli
{
// A prompt submitted is working, a permission or input question is waiting
// with the question, the end of a turn is finished with Claude's last line.
// Nullopt for any other hook event, or input that is not a hook's JSON.
std::optional<PaneStateReport> readClaudeHookReport(const std::string& hookInputJson,
                                                    const std::string& paneId);
} // namespace term::cli
