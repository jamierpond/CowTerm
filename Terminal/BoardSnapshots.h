#pragma once

#include "AgentBoard.h"

#include <optional>
#include <string>

namespace term
{
// The board from outside the app: every running claude whose environment
// names a CowTerm pane gets that pane. Blocks on `claude agents`.
BoardSnapshot collectLiveBoardSnapshot(const BoardQuery& baseQuery);

// Renders the task board at five window shapes into outputDirectory: fixture
// scenes, plus a scene from the real `claude agents` listing when liveQuery is
// given (every claude whose environment names a CowTerm pane). Prints each
// PNG's path; returns a process exit code.
int renderBoardSnapshots(const std::string& outputDirectory,
                         const std::optional<BoardQuery>& liveQuery);
} // namespace term
