// ct: CowTerm's command-line mode. `ct` is a symlink (or alias) to the CowTerm
// executable itself, so the CLI is the same binary as the app.

#include "Cli.h"
#include "CliParsing.h"
#include "CliPlatform.h"
#include "CowTermVersion.h"
#include "NotifyUserClient.h"
#include <PermissionsPanel/Permissions.h>
#include "Protocol.h"
#include "Bridge.h"
#include "Config.h"
#include "RemoteCommands.h"
#include "RemoteLocation.h"

#include <eacp/Core/Platform/ScopedAutoReleasePool.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace term::cli
{
namespace
{
// Exit codes agents can branch on.
constexpr int exitOk = 0;
constexpr int exitFail = 1;

// `ct notify-user` has its own: 0 answered, 1 dismissed, 2 usage, 4 no app.
constexpr int exitNotifyUserDismissed = 1;
constexpr int exitNotifyUserUsage = 2;
constexpr int exitNotifyUserAppUnreachable = 4;

constexpr auto usage = R"(usage: ct <command>

  notify-user --content <text> [--options <a> <b> ...]
              [--allow-type-response] [--title <text>] [--pane <id>]
                                  ask the user in a window over everything;
                                  prints the chosen option or typed answer

  bridge                          relay this machine's session daemon over
                                  stdin/stdout (CowTerm runs it through ssh)
  install-remote <host>           build and install ct + cowtermd on host
                                  over ssh, for remote sessions
  update-remotes                  install-remote for every host in searchDirs

  version
  --reset-permissions [service]   tccutil reset <service> (default All) for
                                  CowTerm's bundle id; runs from any shell

Exit codes: 0 ok, 1 error.
notify-user: 0 answered, 1 dismissed, 2 usage error, 4 CowTerm not running.
Its pane defaults to $COWTERM_PANE, which every CowTerm pane sets.
)";

int fail(const std::string& message)
{
    std::fprintf(stderr, "ct: %s\n", message.c_str());
    return exitFail;
}

int failNotifyUserUsage(const std::string& message)
{
    std::fprintf(stderr, "ct notify-user: %s (see ct help)\n", message.c_str());
    return exitNotifyUserUsage;
}

int exitCodeForNotifyUserOutcome(const NotifyUserOutcome& outcome)
{
    if (!outcome.wasAnswered)
    {
        std::fprintf(stderr,
                     "ct notify-user: CowTerm is not running or went away before "
                     "the user answered\n");
        return exitNotifyUserAppUnreachable;
    }

    if (!outcome.reply.answer)
        return exitNotifyUserDismissed;

    std::printf("%s\n", outcome.reply.answer->c_str());
    std::fflush(stdout);
    return exitOk;
}

int notifyUser(const std::vector<std::string>& args)
{
    const auto* pane = std::getenv(proto::paneEnvironmentVariable);
    const auto defaultPaneId = pane != nullptr ? std::string {pane} : std::string {};
    const auto parsed = parseNotifyUserArguments(args, defaultPaneId);

    if (!parsed.request)
        return failNotifyUserUsage(parsed.usageError);

    return askRunningAppAndWait(*parsed.request, exitCodeForNotifyUserOutcome);
}

using Arguments = std::vector<std::string>;

Arguments dropFirst(const Arguments& args)
{
    return Arguments(args.begin() + 1, args.end());
}

int printUsage(const Arguments&)
{
    std::fputs(usage, stdout);
    return exitOk;
}

int printVersion(const Arguments&)
{
    std::printf("ct %s (%s)\n", appVersion, versionTag().c_str());
    return exitOk;
}

// Wipes CowTerm's own grants, which is the same from any terminal.
int resetPermissions(const Arguments& args)
{
    const auto service = args.size() > 1 ? args[1] : std::string {"All"};
    auto error = std::string {};

    if (!pond::permissions::platform::reset(service, error))
        return fail("reset failed: " + error);

    std::printf("reset %s for %s\n",
                service.c_str(),
                pond::permissions::platform::bundleIdentifier().c_str());
    return exitOk;
}

// Talks to the running app, not the OS; no grant involved, so it runs
// from any terminal.
int notifyUserCommand(const Arguments& args)
{
    return notifyUser(dropFirst(args));
}

int bridge(const Arguments&)
{
    return bridge::runBridge(versionTag());
}

int installRemote(const Arguments& args)
{
    return remote::runInstallRemoteCommand(dropFirst(args));
}

int updateRemotes(const Arguments&)
{
    return remote::runUpdateRemotesCommand(remote::collectRemoteHosts(loadConfig().searchDirs));
}

struct Command
{
    std::string_view verb;
    int (*run)(const Arguments& args);
};

constexpr auto commandsRunAnywhere = std::array {
    Command {"help", printUsage},
    Command {"--help", printUsage},
    Command {"-h", printUsage},
    Command {"version", printVersion},
    Command {"--version", printVersion},
    Command {"--reset-permissions", resetPermissions},
    Command {"notify-user", notifyUserCommand},
    Command {"bridge", bridge},
    Command {"install-remote", installRemote},
    Command {"update-remotes", updateRemotes},
};

int dispatch(const Arguments& args)
{
    if (args.empty())
        return printUsage(args);

    const auto found = std::ranges::find(commandsRunAnywhere, args[0], &Command::verb);

    if (found != commandsRunAnywhere.end())
        return found->run(args);

    return fail("unknown command: " + args[0] + " (see ct help)");
}
} // namespace
} // namespace term::cli

namespace term
{
// `ct` is the stem, so the name matches ct.exe (and ct.cmd's target) too.
bool isCliInvocation(int argc, char** argv)
{
    if (argc > 0 && fs::path {argv[0]}.stem() == "ct")
        return true;

    if (argc < 2)
        return false;

    const auto verb = std::string {argv[1]};
    for (const auto* known: {"notify-user",
                             "help",
                             "--help",
                             "-h",
                             "version",
                             "--version",
                             "--reset-permissions",
                             "bridge",
                             "install-remote",
                             "update-remotes"})
        if (verb == known)
            return true;

    return false;
}

int runCli(int argc, char** argv)
{
    auto pool = eacp::ScopedAutoReleasePool {};
    cli::platform::attachToTerminal();
    return cli::dispatch(std::vector<std::string>(argv + 1, argv + argc));
}
} // namespace term
