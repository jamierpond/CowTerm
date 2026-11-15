#include "BridgeRelay.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term::bridge;

namespace
{
struct RecordingRelay
{
    RecordingRelay()
    {
        relay.sendToDaemon = [this](const std::string& message) { toDaemon.push_back(message); };
        relay.writeToApp = [this](const std::string& bytes) { toApp += bytes; };
    }

    BridgeRelay relay;
    std::vector<std::string> toDaemon;
    std::string toApp;
};
} // namespace

auto appFramesReachTheDaemonWhole = test("BridgeRelay/appFramesReachTheDaemonWhole") = []
{
    auto recording = RecordingRelay {};
    const auto spawn = std::string {"spawn p1 80 24 Grazer\n/home/x/Grazer"};
    const auto write = std::string {"write p1\nls\n"};
    const auto bytes = encodeFrame(spawn) + encodeFrame(write);

    for (const auto byte: bytes)
        check(recording.relay.receiveFromApp(std::string_view {&byte, 1}));

    check(recording.toDaemon == std::vector<std::string> {spawn, write});
};

auto daemonMessagesReachTheAppFramed = test("BridgeRelay/daemonMessagesReachTheAppFramed") = []
{
    auto recording = RecordingRelay {};
    recording.relay.announceReady("abc1234");
    recording.relay.receiveFromDaemon("output p1\nhello");

    auto decoder = FrameDecoder {};
    const auto messages = decoder.append(recording.toApp);

    check(messages.size() == 2);
    check(parseHelloMessage(messages[0]).has_value());
    check(messages[1] == "output p1\nhello");
};

auto garbageFromTheAppStopsTheRelay = test("BridgeRelay/garbageFromTheAppStopsTheRelay") = []
{
    auto recording = RecordingRelay {};

    check(!recording.relay.receiveFromApp("not a frame\n"));
    check(!recording.relay.receiveFromApp(encodeFrame("info")));
    check(recording.toDaemon.empty());
};
