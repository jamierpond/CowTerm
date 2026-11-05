#pragma once

#include "AgentBoard.h"

#include <string>

#include <optional>

namespace term
{
// Where claude is (PATH, then ~/.local/bin) and where its transcripts are
// (CLAUDE_CONFIG_DIR, else ~/.claude). Empty when claude is not installed.
std::optional<BoardQuery> makeClaudeQueryFromEnvironment();
// What `n` on the board needs to start an agent: where projects live and the
// git and gh to clone and branch with. A GUI launch has a short PATH, so the
// usual install folders are searched after it.
struct StartAgentSettings
{
    std::string projectsDirectory;
    std::string gitExecutable;
    std::string ghExecutable;
};

StartAgentSettings makeStartAgentSettingsFromEnvironment();
} // namespace term
