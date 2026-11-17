#include "RecordingBridgeLink.h"

#include "Multiplexer/BridgeFraming.h"

#include <NanoTest/NanoTest.h>

#include <vector>

using namespace nano;
using namespace term;

namespace
{
constexpr auto host = "jamie@tamby";
constexpr auto ctVersion = "0.1.0+abc";

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

std::string makeHelloFrame()
{
    return bridge::encodeFrame(bridge::makeHelloMessage(ctVersion));
}

struct WatchedLink
{
    WatchedLink()
    {
        link.onReady = [this] { isReady = true; };
        link.onFailed = [this](const std::string& reason) { failureReason = reason; };
        link.onDisconnected = [this] { isDisconnected = true; };
        link.onMessage = [this](std::string message) { messages.push_back(std::move(message)); };
    }

    RecordingBridgeLink link {host};
    bool isReady = false;
    bool isDisconnected = false;
    std::string failureReason;
    std::vector<std::string> messages;
};
} // namespace

auto helloMakesTheLinkReady = test("BridgeLink/helloMakesTheLinkReady") = []
{
    auto watched = WatchedLink {};
    watched.link.deliverBytes(makeHelloFrame());

    check(watched.isReady);
    check(watched.link.isConnected());
};

auto messagesAreSentOnlyOnceConnected = test("BridgeLink/messagesAreSentOnlyOnceConnected") = []
{
    auto watched = WatchedLink {};
    watched.link.send("info");
    check(watched.link.writtenBytes.empty());

    watched.link.deliverBytes(makeHelloFrame());
    watched.link.send("info");
    check(watched.link.writtenBytes == bridge::encodeFrame("info"));
};

auto messagesAfterHelloReachOnMessage = test("BridgeLink/messagesAfterHelloReachOnMessage") = []
{
    auto watched = WatchedLink {};
    const auto bytes = makeHelloFrame() + bridge::encodeFrame("output pane\nhi");
    constexpr auto splitPoint = std::size_t {5};

    watched.link.deliverBytes(bytes.substr(0, splitPoint));
    watched.link.deliverBytes(bytes.substr(splitPoint));

    check(watched.messages == std::vector<std::string> {"output pane\nhi"});
};

auto neighbouringProtocolVersionIsAccepted = test("BridgeLink/neighbouringProtocolVersionIsAccepted") = []
{
    auto watched = WatchedLink {};
    const auto newerVersion = bridge::protocolVersion + 1;
    const auto hello = "bridge-hello " + std::to_string(newerVersion) + " " + ctVersion;
    watched.link.deliverBytes(bridge::encodeFrame(hello));

    check(watched.isReady);
};

auto distantProtocolVersionNamesTheInstallFix = test("BridgeLink/distantProtocolVersionNamesTheInstallFix") = []
{
    constexpr auto versionsAhead = 2;
    auto watched = WatchedLink {};
    const auto muchNewerVersion = bridge::protocolVersion + versionsAhead;
    const auto hello = "bridge-hello " + std::to_string(muchNewerVersion) + " " + ctVersion;
    watched.link.deliverBytes(bridge::encodeFrame(hello));

    check(!watched.isReady);
    check(contains(watched.failureReason, "ct install-remote"));
    check(watched.link.isTransportStopped);
    check(watched.isDisconnected);
};

auto loginBannerIsNotABridge = test("BridgeLink/loginBannerIsNotABridge") = []
{
    auto watched = WatchedLink {};
    watched.link.deliverBytes("Welcome to Ubuntu\n");

    check(contains(watched.failureReason, "sent something else: Welcome to Ubuntu"));
};

auto bridgeErrorNamesTheHost = test("BridgeLink/bridgeErrorNamesTheHost") = []
{
    auto watched = WatchedLink {};
    watched.link.deliverBytes(bridge::encodeFrame(bridge::makeErrorMessage("no session daemon")));

    check(watched.failureReason == std::string {"no session daemon on "} + host);
};

auto onlyTheFirstFailureIsReported = test("BridgeLink/onlyTheFirstFailureIsReported") = []
{
    auto watched = WatchedLink {};
    watched.link.failTransport("first");
    watched.link.failTransport("second");

    check(watched.failureReason == "first");
};
