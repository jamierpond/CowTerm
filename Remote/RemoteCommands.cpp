#include "RemoteCommands.h"

#include "RemoteInstall.h"

#include <cstdio>

namespace term::remote
{
namespace
{
constexpr int exitOk = 0;
constexpr int exitFail = 1;
constexpr int exitUsage = 2;

void printResult(const RemoteInstallResult& result)
{
    const auto* outcome = result.succeeded ? "updated" : "FAILED";
    std::printf("== %s: %s\n%s", result.host.c_str(), outcome, result.output.c_str());

    if (!result.output.empty() && result.output.back() != '\n')
        std::printf("\n");

    std::fflush(stdout);
}
} // namespace

int runInstallRemoteCommand(const std::vector<std::string>& arguments)
{
    if (arguments.size() != 1)
    {
        std::fprintf(stderr, "usage: ct install-remote <host>\n");
        return exitUsage;
    }

    const auto result = installOnHost(arguments.front());
    printResult(result);
    return result.succeeded ? exitOk : exitFail;
}

int runUpdateRemotesCommand(const std::vector<std::string>& hosts)
{
    if (hosts.empty())
    {
        std::printf("no remote hosts in searchDirs (~/.config/cowterm.json)\n");
        return exitOk;
    }

    auto allSucceeded = true;

    for (const auto& result: installOnHosts(hosts))
    {
        printResult(result);
        allSucceeded = allSucceeded && result.succeeded;
    }

    return allSucceeded ? exitOk : exitFail;
}
} // namespace term::remote
