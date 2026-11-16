#include "BridgeRelay.h"

#include "ProjectScan.h"
#include "Protocol.h"

namespace term::bridge
{
bool BridgeRelay::receiveFromApp(std::string_view bytes)
{
    for (const auto& message: decoder.append(bytes))
    {
        if (const auto answer = answerLocally(message))
            writeToApp(encodeFrame(*answer));
        else
            sendToDaemon(message);
    }

    return !decoder.hasFailed();
}

void BridgeRelay::receiveFromDaemon(const std::string& message) const
{
    writeToApp(encodeFrame(message));
}

void BridgeRelay::announceReady(std::string_view ctVersion) const
{
    writeToApp(encodeFrame(makeHelloMessage(ctVersion)));
}

void BridgeRelay::announceFailure(std::string_view reason) const
{
    writeToApp(encodeFrame(makeErrorMessage(reason)));
}
} // namespace term::bridge

namespace term::bridge
{
namespace
{
std::vector<std::string> splitRoots(const std::string& payload, const std::string& homeDirectory)
{
    auto roots = std::vector<std::string> {};
    auto start = std::size_t {0};

    while (start < payload.size())
    {
        const auto end = std::min(payload.find('\n', start), payload.size());
        auto root = payload.substr(start, end - start);

        if (!root.empty() && root[0] == '~')
            root.replace(0, 1, homeDirectory);

        if (!root.empty())
            roots.push_back(std::move(root));

        start = end + 1;
    }

    return roots;
}
} // namespace

std::optional<std::string> BridgeRelay::answerLocally(const std::string& message) const
{
    const auto parsed = proto::parse(message);

    if (parsed.verb != "scan")
        return std::nullopt;

    auto listing = std::string {};

    for (const auto& path: listProjectDirectories(splitRoots(parsed.payload, homeDirectory)))
        listing += path + "\n";

    return proto::make("scan-result", {}, listing);
}
} // namespace term::bridge
