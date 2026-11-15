#include "RemoteLocation.h"

#include <algorithm>

namespace term::remote
{
namespace
{
bool isWindowsDrivePrefix(std::size_t colon)
{
    constexpr auto driveLetterLength = std::size_t {1};
    return colon == driveLetterLength;
}

bool isHostSeparator(std::size_t colon)
{
    return colon != std::string::npos && colon != 0 && !isWindowsDrivePrefix(colon);
}

bool startsLikeLocalPath(const std::string& entry)
{
    return entry.empty() || entry[0] == '~' || entry[0] == '/' || entry[0] == '.';
}
} // namespace

std::optional<RemoteSearchDir> parseRemoteSearchDir(const std::string& entry)
{
    if (startsLikeLocalPath(entry))
        return std::nullopt;

    const auto colon = entry.find(':');

    if (!isHostSeparator(colon))
        return std::nullopt;

    const auto host = entry.substr(0, colon);

    if (host.find('/') != std::string::npos || host.find('\\') != std::string::npos)
        return std::nullopt;

    auto path = entry.substr(colon + 1);

    if (path.empty())
        path = "~";

    return RemoteSearchDir {host, path};
}

std::vector<std::string> collectRemoteHosts(const std::vector<std::string>& searchDirs)
{
    auto hosts = std::vector<std::string> {};

    for (const auto& entry: searchDirs)
    {
        const auto remote = parseRemoteSearchDir(entry);

        if (remote && std::ranges::find(hosts, remote->host) == hosts.end())
            hosts.push_back(remote->host);
    }

    return hosts;
}

std::string getHostLabel(const std::string& host)
{
    const auto at = host.find('@');

    if (at == std::string::npos)
        return host;

    return host.substr(at + 1);
}

std::string makeRemoteProjectDir(const std::string& host, const std::string& path)
{
    return host + ":" + path;
}
} // namespace term::remote
