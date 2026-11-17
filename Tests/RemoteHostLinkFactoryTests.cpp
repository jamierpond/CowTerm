#include "RecordingBridgeLink.h"

#include "Multiplexer/BridgeFraming.h"
#include "Multiplexer/RemoteHosts.h"

#include <NanoTest/NanoTest.h>

#include <functional>
#include <optional>
#include <vector>

using namespace nano;
using namespace term;

namespace
{
constexpr auto host = "jamie@tamby";

// Hands RemoteHost a RecordingBridgeLink and keeps a handle on it.
struct LinkFactory
{
    BridgeLinkFactory makeFactory()
    {
        return [this](const std::string& requestedHost)
        {
            requestedHosts.push_back(requestedHost);
            auto link = std::make_unique<RecordingBridgeLink>(requestedHost);
            lastLink = std::ref(*link);
            return link;
        };
    }

    RecordingBridgeLink& getLastLink() { return lastLink->get(); }

    std::vector<std::string> requestedHosts;
    std::optional<std::reference_wrapper<RecordingBridgeLink>> lastLink;
};

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}
} // namespace

auto connectAsksTheFactoryForTheHostsLink = test("RemoteHost/connectAsksTheFactoryForTheHostsLink") = []
{
    auto factory = LinkFactory {};
    auto remote = RemoteHost {host, {"~/projects"}, factory.makeFactory()};

    check(factory.requestedHosts == std::vector<std::string> {host});
    check(remote.getStatus() == RemoteHost::Status::Connecting);
};

auto readyLinkConnectsAndScans = test("RemoteHost/readyLinkConnectsAndScans") = []
{
    auto factory = LinkFactory {};
    auto remote = RemoteHost {host, {"~/projects"}, factory.makeFactory()};
    auto& link = factory.getLastLink();

    link.deliverBytes(bridge::encodeFrame(bridge::makeHelloMessage("0.1.0")));

    check(remote.getStatus() == RemoteHost::Status::Connected);
    check(contains(link.writtenBytes, "scan\n~/projects\n"));
};

auto failedLinkMarksTheHostUnreachable = test("RemoteHost/failedLinkMarksTheHostUnreachable") = []
{
    auto factory = LinkFactory {};
    auto remote = RemoteHost {host, {"~/projects"}, factory.makeFactory()};
    constexpr auto reason = "unreachable (ssh: connect to host tamby port 22: Operation timed out)";

    factory.getLastLink().failTransport(reason);

    check(remote.getStatus() == RemoteHost::Status::Unreachable);
    check(remote.getFailureReason() == reason);
};
