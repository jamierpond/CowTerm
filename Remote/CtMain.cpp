// ct without the app: what a remote machine runs. `ssh host ct bridge`
// carries the session daemon to CowTerm on another machine.

#include "Bridge.h"
#include "CliParsing.h"
#include "Config.h"
#include "NotifyUserClient.h"
#include "Protocol.h"
#include "CowTermVersion.h"
#include "RemoteCommands.h"
#include "RemoteLocation.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
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
  notify-user --content <text> [--options <a> <b> ...]
              [--allow-type-response] [--title <text>] [--pane <id>]
                          ask the user in CowTerm, on the machine whose
                          window shows this pane (through ct bridge)
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

constexpr int exitNotifyUserDismissed = 1;
constexpr int exitNotifyUserUsage = 2;
constexpr int exitNotifyUserAppUnreachable = 4;

int exitCodeForNotifyUserOutcome(const term::NotifyUserOutcome& outcome)
{
    if (!outcome.wasAnswered)
    {
        std::fprintf(stderr, "ct notify-user: no CowTerm is attached to this machine\n");
        return exitNotifyUserAppUnreachable;
    }

    if (!outcome.reply.answer)
        return exitNotifyUserDismissed;

    std::printf("%s\n", outcome.reply.answer->c_str());
    return exitOk;
}

int notifyUser(const Arguments& arguments)
{
    const auto* pane = std::getenv(term::proto::paneEnvironmentVariable);
    const auto parsed =
        term::cli::parseNotifyUserArguments(arguments, pane != nullptr ? pane : "");

    if (!parsed.request)
    {
        std::fprintf(stderr, "ct notify-user: %s (see ct help)\n", parsed.usageError.c_str());
        return exitNotifyUserUsage;
    }

    return term::askRunningAppAndWait(*parsed.request, exitCodeForNotifyUserOutcome);
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
    Command {"notify-user", notifyUser},
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
