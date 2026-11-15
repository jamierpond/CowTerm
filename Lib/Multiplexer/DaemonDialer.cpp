#include "DaemonDialer.h"

#include "Protocol.h"

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Threads/Async.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>

#include <array>
#include <filesystem>

namespace term::daemon
{
using namespace eacp;

namespace
{
// The daemon is either already there or not installed at all; after a
// launch, it gets time to start.
constexpr auto firstDialTimeout = Time::MS {600};
constexpr auto dialAfterLaunchTimeout = Time::MS {4000};

using SharedMessenger = std::shared_ptr<std::unique_ptr<IPC::Messenger>>;

SharedMessenger makeDial(Time::MS timeout)
{
    return std::make_shared<std::unique_ptr<IPC::Messenger>>(
        std::make_unique<IPC::Messenger>(proto::serverName(), timeout));
}

void dialAfterLaunch(const DialedDaemon& whenDone)
{
    launchDaemon();
    auto second = makeDial(dialAfterLaunchTimeout);

    (*second)->onConnected = [second, whenDone] { whenDone(std::move(*second)); };

    (*second)->onDisconnected = [second, whenDone]
    {
        // Resetting destroys this very handler, captures included.
        const auto report = whenDone;
        second->reset();
        report(nullptr);
    };
}
} // namespace

std::string findDaemonExecutable()
{
    const auto self = toStdPath(Files::executablePath());

    if (self.empty())
        return {};

    const auto extension = self.extension().string();

    for (const auto* name: {"cowtermd", "CowTermDaemon"})
    {
        const auto candidate = self.parent_path() / (name + extension);
        auto error = std::error_code {};

        if (std::filesystem::exists(candidate, error))
            return FilePath {candidate}.str();
    }

    return {};
}

void launchDaemon()
{
    const auto executable = findDaemonExecutable();

    if (executable.empty())
        return;

    auto options = Processes::ProcessOptions {};
    options.executable = executable;
    options.detached = true;

    // Detached: destroying the handle leaves the daemon running.
    auto daemon = Processes::Process {options};
}

void dialOrLaunchDaemon(const DialedDaemon& whenDone)
{
    auto first = makeDial(firstDialTimeout);

    (*first)->onConnected = [first, whenDone] { whenDone(std::move(*first)); };

    (*first)->onDisconnected = [first, whenDone]
    {
        dialAfterLaunch(whenDone);

        // Keep the dying first messenger alive until this handler returns.
        Threads::callAsync([first] {});
    };
}
} // namespace term::daemon
