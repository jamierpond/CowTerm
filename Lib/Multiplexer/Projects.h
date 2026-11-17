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

// Depth-1 directories under each local search dir (the sessionizer scan),
// deduped. Hidden dirs are included so dotfile repos like ~/.config are
// reachable; the search dirs themselves are listed too. Remote entries
// (host:path) are scanned by their host (RemoteHosts.h).
std::vector<ProjectDir> scanProjects(const AppConfig& config);

// Session name for a project path: basename with dots swapped for
// underscores, matching the tmux-sessionizer convention.
std::string sessionNameFor(const std::string& path);

// The name the Mac gives a project's session: "Grazer", or "tamby/Grazer"
// for a project dir on tamby ("tamby:/home/x/Grazer").
std::string getSessionNameForProject(const std::string& projectDir);

// "/home/jamie/projects/x", "/Users/jamie/x" or "C:/Users/jamie/x" with the
// home part as ~, the way local rows show theirs.
std::string compactRemoteHomePath(const std::string& path);
} // namespace term
