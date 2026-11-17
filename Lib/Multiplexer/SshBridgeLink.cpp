#include "SshBridgeLink.h"

#include "RemoteLocation.h"

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Threads/Async.h>

#include <thread>

namespace term
{
using namespace eacp;

namespace
{
constexpr auto serverAliveIntervalSeconds = "ServerAliveInterval=5";
constexpr auto serverAliveCountMax = "ServerAliveCountMax=3";
constexpr auto connectTimeout = "ConnectTimeout=10";

Processes::ProcessOptions makeBridgeProcessOptions(const std::string& host)
{
    auto options = Processes::ProcessOptions {};
    options.executable = "ssh";
    options.arguments = {"-o", "BatchMode=yes",
                         "-o", serverAliveIntervalSeconds,
                         "-o", serverAliveCountMax,
                         "-o", connectTimeout,
                         host,
                         remote::bridgeCommand};
    return options;
}
} // namespace

SshBridgeLink::SshBridgeLink(std::string hostToUse)
    : BridgeLink(std::move(hostToUse))
{
    auto options = makeBridgeProcessOptions(getHost());
    options.onOutput = [guard = std::weak_ptr<bool> {alive}, this](std::string_view chunk)
    {
        Threads::callAsync([guard, this, bytes = std::string {chunk}]
                           {
                               if (!guard.expired())
                                   receiveBytes(bytes);
                           });
    };

    process = std::make_shared<Processes::Process>(options);

    if (!process->launched())
    {
        Threads::callAsync([guard = std::weak_ptr<bool> {alive}, this]
                           {
                               if (!guard.expired())
                                   fail("could not run ssh");
                           });
        return;
    }

    auto waiter = std::thread(
        [running = process, guard = std::weak_ptr<bool> {alive}, this]
        {
            const auto exitCode = running->wait();
            auto errorOutput = running->errorOutput();

            Threads::callAsync([guard, this, exitCode, errorOutput = std::move(errorOutput)]
                               {
                                   if (!guard.expired())
                                       handleExit(exitCode, errorOutput);
                               });
        });

    waiter.detach();
}

SshBridgeLink::~SshBridgeLink()
{
    *alive = false;

    if (process != nullptr)
        process->kill();
}

void SshBridgeLink::writeBytes(const std::string& bytes)
{
    process->writeToInput(bytes);
}

void SshBridgeLink::stopTransport()
{
    if (process != nullptr)
        process->kill();
}

void SshBridgeLink::handleExit(int exitCode, const std::string& errorOutput)
{
    if (hasFinished())
        return;

    fail(describeBridgeFailure(getHost(), exitCode, errorOutput));
}

std::unique_ptr<BridgeLink> makeSshBridgeLink(const std::string& host)
{
    return std::make_unique<SshBridgeLink>(host);
}
} // namespace term
