#include "BridgeLink.h"

#include "Protocol.h"
#include "RemoteInstall.h"

namespace term
{
namespace
{
constexpr int commandNotFoundExitCode = 127;

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

BridgeLink::BridgeLink(std::string hostToUse)
    : host(std::move(hostToUse))
{
}

void BridgeLink::send(const std::string& message)
{
    if (connected)
        writeBytes(bridge::encodeFrame(message));
}

void BridgeLink::receiveBytes(const std::string& bytes)
{
    onBytesReceived();

    for (auto& message: decoder.append(bytes))
        receiveMessage(std::move(message));

    if (decoder.hasFailed() && !finished)
        fail("ct bridge on " + host + " sent something else: " + lastLine(decoder.getUnparsedBytes()));
}

void BridgeLink::receiveMessage(std::string message)
{
    if (connected)
        return onMessage(std::move(message));

    if (const auto hello = bridge::parseHelloMessage(message))
        return acceptHello(*hello);

    if (const auto reason = bridge::parseErrorMessage(message))
        fail(*reason + " on " + host);
}

void BridgeLink::acceptHello(const bridge::BridgeHello& hello)
{
    if (!proto::isNeighbouringProtocolVersion(bridge::protocolVersion, hello.protocolVersion))
    {
        return fail("ct on " + host + " speaks bridge protocol " + std::to_string(hello.protocolVersion)
                    + ", this CowTerm " + std::to_string(bridge::protocolVersion) + "; run: "
                    + remote::describeInstallFix(host));
    }

    connected = true;
    onReady();
}

void BridgeLink::fail(const std::string& reason)
{
    if (finished)
        return;

    finished = true;
    connected = false;
    stopTransport();
    onFailed(reason);
    onDisconnected();
}
} // namespace term
