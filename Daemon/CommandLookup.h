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

// The PATH a command pane runs with. On POSIX, what the user's interactive
// login shell sets up (zsh reads .zshrc only when interactive, and a daemon
// started over ssh has a bare PATH of its own); on Windows, the daemon's.
std::string findCommandSearchPath();

// Whether the command's program is on searchPath (PATHEXT's extensions too,
// on Windows).
bool isCommandInstalled(const std::string& command, const std::string& searchPath);
} // namespace term
