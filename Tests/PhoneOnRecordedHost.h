#pragma once

#include "RecordingBridgeLink.h"

#include "Multiplexer/BridgeFraming.h"
#include "Multiplexer/Protocol.h"
#include "Phone/PhoneConnection.h"

#include <algorithm>
#include <unistd.h>

// PhoneConnection over RecordingBridgeLinks, one per host, with a clock the
// test moves by hand; shared by the connection and overview tests.
namespace phonetest
{
using namespace term;
using namespace term::phone;

constexpr auto startTime = Milliseconds {1000};

inline PhoneSettings makeSettings(const std::string& explicitPaneId)
{
    auto settings = PhoneSettings {};
    settings.destination = {"jamie", "tamby"};
    settings.explicitPaneId = explicitPaneId;
    const auto scratchName = "cowterm-phone-connection-tests-" + std::to_string(::getpid());
    settings.storedScreenPath = std::filesystem::temp_directory_path() / scratchName / "last-screen.json";
    return settings;
}

inline std::string makeInfoLine(const std::string& paneId, const std::string& sessionName)
{
    return paneId + "\tzsh\t/tmp\t" + sessionName + "\n";
}

// A PhoneConnection whose host is a RecordingBridgeLink and whose clock the
// test moves by hand.
struct PhoneOnRecordedHost
{
    explicit PhoneOnRecordedHost(const PhoneSettings& settings)
        : connection(settings, factory.makeFactory(), [this] { return now; })
    {
        connection.onPaneChosen = [this](const ChosenPane& pane) { chosenPaneId = pane.paneId; };
        connection.onPaneStatus = [this](const std::string& status) { paneStatus = status; };
    }

    // The first destination's link.
    RecordingBridgeLink& getLink() { return factory.getLinkFor(connection.getHost()); }

    void bringLinkUp() { bringLinkUpFor(connection.getHost()); }
    void answer(const std::string& message) { answerFrom(connection.getHost(), message); }
    void answerInfo(const std::string& lines) { answer(proto::make("info", {}, lines)); }

    void bringLinkUpFor(const std::string& host) { answerFrom(host, bridge::makeHelloMessage("0.1.0")); }

    void answerFrom(const std::string& host, const std::string& message)
    {
        factory.getLinkFor(host).deliverBytes(bridge::encodeFrame(message));
    }

    // The pane view's start, as TerminalView does with its shell.
    void openPaneView(const std::string& paneId)
    {
        paneShell = connection.makeShell(paneId);
        paneShell->start({}, [](const std::string&) {}, [] {});
    }

    std::vector<std::string> readSentMessages(const std::string& host)
    {
        auto decoder = bridge::FrameDecoder {};
        return decoder.append(factory.getLinkFor(host).writtenBytes);
    }

    bool hasSentMessageStartingWith(const std::string& start) { return hasSentTo(connection.getHost(), start); }

    bool hasSentTo(const std::string& host, const std::string& start)
    {
        const auto sent = readSentMessages(host);
        return std::ranges::any_of(sent, [&](const std::string& message) { return message.starts_with(start); });
    }

    RecordingLinkFactory factory;
    Milliseconds now = startTime;
    PhoneConnection connection;
    std::unique_ptr<Shell> paneShell;
    std::string chosenPaneId;
    std::string paneStatus;
};
} // namespace phonetest
