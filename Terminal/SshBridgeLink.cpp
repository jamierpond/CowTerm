#include "SshBridgeLink.h"

#include "RemoteInstall.h"
#include "RemoteLocation.h"

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Threads/Async.h>

#include <thread>

namespace term
{
using namespace eacp;

namespace
{
constexpr int sshFailedExitCode = 255;
constexpr int commandNotFoundExitCode = 127;
constexpr auto serverAliveIntervalSeconds = "ServerAliveInterval=5";
constexpr auto serverAliveCountMax = "ServerAliveCountMax=3";
constexpr auto connectTimeout = "ConnectTimeout=10";

bool contains(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}

std::string lastLine(const std::string& text)
{
    auto end = text.find_last_not_of("\r\n");

    if (end == std::string::npos)
        return {};

    const auto previousNewline = text.find_last_of('\n', end);
    const auto first = previousNewline == std::string::npos ? 0 : previousNewline + 1;
    return text.substr(first, end + 1 - first);
}

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

std::string describeBridgeFailure(const std::string& host,
                                  int exitCode,
                                  const std::string& errorOutput)
{
    const auto fix = remote::describeInstallFix(host);
    const auto detail = lastLine(errorOutput);

    if (detail.empty())
        return "connection lost";

    if (contains(errorOutput, "Permission denied") || contains(errorOutput, "Host key verification"))
        return "ssh to " + host + " refused key auth: " + detail;

    if (exitCode == commandNotFoundExitCode || contains(errorOutput, "No such file"))
        return "ct is not installed on " + host + "; run: " + fix;

    if (exitCode == sshFailedExitCode)
        return "unreachable (" + detail + ")";

    return "ct bridge on " + host + " stopped: " + detail;
}

SshBridgeLink::SshBridgeLink(std::string hostToUse)
    : host(std::move(hostToUse))
{
    auto options = makeBridgeProcessOptions(host);
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

void SshBridgeLink::send(const std::string& message)
{
    if (connected)
        process->writeToInput(bridge::encodeFrame(message));
}

void SshBridgeLink::receiveBytes(const std::string& bytes)
{
    for (auto& message: decoder.append(bytes))
        receiveMessage(std::move(message));

    if (decoder.hasFailed() && !finished)
        fail("ct bridge on " + host + " sent something else: " + lastLine(decoder.getUnparsedBytes()));
}

void SshBridgeLink::receiveMessage(std::string message)
{
    if (connected)
        return onMessage(std::move(message));

    if (const auto hello = bridge::parseHelloMessage(message))
        return acceptHello(*hello);

    if (const auto reason = bridge::parseErrorMessage(message))
        fail(*reason + " on " + host);
}

void SshBridgeLink::acceptHello(const bridge::BridgeHello& hello)
{
    if (hello.protocolVersion != bridge::protocolVersion)
    {
        return fail("ct on " + host + " speaks bridge protocol " + std::to_string(hello.protocolVersion)
                    + ", this CowTerm " + std::to_string(bridge::protocolVersion) + "; run: "
                    + remote::describeInstallFix(host));
    }

    connected = true;
    onReady();
}

void SshBridgeLink::handleExit(int exitCode, const std::string& errorOutput)
{
    if (finished)
        return;

    fail(describeBridgeFailure(host, exitCode, errorOutput));
}

void SshBridgeLink::fail(const std::string& reason)
{
    if (finished)
        return;

    finished = true;
    connected = false;

    if (process != nullptr)
        process->kill();

    onFailed(reason);
    onDisconnected();
}
} // namespace term
