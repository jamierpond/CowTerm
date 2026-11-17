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

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}
} // namespace

auto connectAsksTheFactoryForTheHostsLink = test("RemoteHost/connectAsksTheFactoryForTheHostsLink") = []
{
    auto factory = RecordingLinkFactory {};
    auto remote = RemoteHost {host, {"~/projects"}, factory.makeFactory()};

    check(factory.requestedHosts == std::vector<std::string> {host});
    check(remote.getStatus() == RemoteHost::Status::Connecting);
};

auto readyLinkConnectsAndScans = test("RemoteHost/readyLinkConnectsAndScans") = []
{
    auto factory = RecordingLinkFactory {};
    auto remote = RemoteHost {host, {"~/projects"}, factory.makeFactory()};
    auto& link = factory.getLastLink();

    link.deliverBytes(bridge::encodeFrame(bridge::makeHelloMessage("0.1.0")));

    check(remote.getStatus() == RemoteHost::Status::Connected);
    check(contains(link.writtenBytes, "scan\n~/projects\n"));
};

auto failedLinkMarksTheHostUnreachable = test("RemoteHost/failedLinkMarksTheHostUnreachable") = []
{
    auto factory = RecordingLinkFactory {};
    auto remote = RemoteHost {host, {"~/projects"}, factory.makeFactory()};
    constexpr auto reason = "unreachable (ssh: connect to host tamby port 22: Operation timed out)";

    factory.getLastLink().failTransport(reason);

    check(remote.getStatus() == RemoteHost::Status::Unreachable);
    check(remote.getFailureReason() == reason);
};

auto reconnectNowAsksForAFreshLink = test("RemoteHost/reconnectNowAsksForAFreshLink") = []
{
    auto factory = RecordingLinkFactory {};
    auto remote = RemoteHost {host, {"~/projects"}, factory.makeFactory()};

    remote.reconnectNow();

    check(factory.requestedHosts == std::vector<std::string> {host, host});
    check(remote.getStatus() == RemoteHost::Status::Connecting);
};

auto messagesRemoteHostIgnoresReachTheApp = test("RemoteHost/messagesRemoteHostIgnoresReachTheApp") = []
{
    auto factory = RecordingLinkFactory {};
    auto remote = RemoteHost {host, {"~/projects"}, factory.makeFactory()};
    auto heardVerb = std::string {};
    remote.onUnhandledMessage = [&](RemoteHost&, const std::string& verb, const std::string&) { heardVerb = verb; };
    auto& link = factory.getLastLink();

    link.deliverBytes(bridge::encodeFrame(bridge::makeHelloMessage("0.1.0")));
    link.deliverBytes(bridge::encodeFrame("last-input pane-1 1794906370945"));

    check(heardVerb == "last-input");
};
