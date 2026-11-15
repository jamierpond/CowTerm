#include "Projects.h"

#include "ProjectScan.h"
#include "RemoteLocation.h"

#include <algorithm>
#include <filesystem>

namespace term
{
namespace fs = std::filesystem;

std::string sessionNameFor(const std::string& path)
{
    auto name = fs::path(path).filename().string();

    if (name.empty())
        name = path;

    std::replace(name.begin(), name.end(), '.', '_');
    return name;
}

std::vector<ProjectDir> scanProjects(const AppConfig& config)
{
    auto roots = std::vector<std::string> {};

    for (const auto& configured: config.searchDirs)
    {
        if (!remote::parseRemoteSearchDir(configured))
            roots.push_back(expandHome(configured));
    }

    auto result = std::vector<ProjectDir> {};

    for (const auto& path: listProjectDirectories(roots))
        result.push_back({path, sessionNameFor(path)});

    return result;
}
} // namespace term
