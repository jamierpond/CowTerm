#include "BridgeRelay.h"

#include <NanoTest/NanoTest.h>

#include <filesystem>

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

auto scanIsAnsweredByTheBridgeItself = test("BridgeRelay/scanIsAnsweredByTheBridgeItself") = []
{
    namespace fs = std::filesystem;
    const auto home = fs::temp_directory_path() / "cowterm-bridge-scan";
    fs::remove_all(home);
    fs::create_directories(home / "projects" / "Grazer");
    fs::create_directories(home / "projects" / ".config");

    auto recording = RecordingRelay {};
    recording.relay.homeDirectory = home.generic_string();
    recording.relay.receiveFromApp(encodeFrame("scan\n~/projects\n/does/not/exist\n"));

    auto decoder = FrameDecoder {};
    const auto messages = decoder.append(recording.toApp);
    const auto root = (home / "projects").generic_string();

    check(recording.toDaemon.empty());
    check(messages.size() == 1);
    check(messages[0].starts_with("scan-result\n" + root + "\n"));
    check(messages[0].find(root + "/Grazer\n") != std::string::npos);
    check(messages[0].find(root + "/.config\n") != std::string::npos);
    fs::remove_all(home);
};
