#pragma once

#include "Config.h"

#include <string>
#include <vector>

namespace term
{
struct ProjectDir
{
    std::string path;
    std::string name;
};

// Depth-1 directories under each configured search dir (the sessionizer
// scan), deduped. Hidden dirs are included so dotfile repos like ~/.config
// are reachable; the search dirs themselves are listed too.
std::vector<ProjectDir> scanProjects(const AppConfig& config);

// Session name for a project path: basename with dots swapped for
// underscores, matching the tmux-sessionizer convention.
std::string sessionNameFor(const std::string& path);
} // namespace term
