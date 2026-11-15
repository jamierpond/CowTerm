// ct without the app: what a remote machine runs. `ssh host ct bridge`
// carries the session daemon to CowTerm on another machine.

#include "Bridge.h"
#include "Config.h"
#include "CowTermVersion.h"
#include "RemoteCommands.h"
#include "RemoteLocation.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using Arguments = std::vector<std::string>;

constexpr int exitOk = 0;
constexpr int exitFail = 1;

constexpr auto usage = R"(usage: ct <command>    (headless: no CowTerm window on this machine)

  bridge                  relay this machine's session daemon over
                          stdin/stdout (CowTerm runs it through ssh)
  install-remote <host>   build and install ct + cowtermd on host over ssh
  update-remotes          install-remote for every host in searchDirs
  version
)";

int printUsage(const Arguments&)
{
    std::fputs(usage, stdout);
    return exitOk;
}

int printVersion(const Arguments&)
{
    std::printf("ct %s (%s, headless)\n", term::appVersion, term::versionTag().c_str());
    return exitOk;
}

int bridge(const Arguments&)
{
    return term::bridge::runBridge(term::versionTag());
}

int installRemote(const Arguments& arguments)
{
    return term::remote::runInstallRemoteCommand(arguments);
}

int updateRemotes(const Arguments&)
{
    return term::remote::runUpdateRemotesCommand(
        term::remote::collectRemoteHosts(term::loadConfig().searchDirs));
}

struct Command
{
    std::string_view verb;
    int (*run)(const Arguments& arguments);
};

constexpr auto commands = std::array {
    Command {"help", printUsage},
    Command {"--help", printUsage},
    Command {"-h", printUsage},
    Command {"version", printVersion},
    Command {"--version", printVersion},
    Command {"bridge", bridge},
    Command {"install-remote", installRemote},
    Command {"update-remotes", updateRemotes},
};
} // namespace

int main(int argc, char** argv)
{
    const auto arguments = Arguments(argv + 1, argv + argc);

    if (arguments.empty())
        return printUsage(arguments);

    const auto found = std::ranges::find(commands, arguments.front(), &Command::verb);

    if (found == commands.end())
    {
        std::fprintf(stderr, "ct: unknown command: %s (see ct help)\n", arguments.front().c_str());
        return exitFail;
    }

    return found->run(Arguments(arguments.begin() + 1, arguments.end()));
}
