#pragma once

#include <string>

namespace term
{
// The program a command line runs: its first word ("lazygit" for "lazygit -p x").
inline std::string getProgramName(const std::string& command)
{
    const auto start = command.find_first_not_of(' ');

    if (start == std::string::npos)
        return {};

    return command.substr(start, command.find(' ', start) - start);
}

// Whether the command's program can be found the way a pane would run it:
// on POSIX through the user's login shell (its PATH, not the daemon's), on
// Windows on PATH with PATHEXT's extensions.
bool isCommandInstalled(const std::string& command);
} // namespace term
