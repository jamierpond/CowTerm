#include "RecordingBridgeLink.h"

#include "Multiplexer/BridgeFraming.h"
#include "Multiplexer/Protocol.h"
#include "Phone/PhoneConnection.h"

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <unistd.h>

using namespace nano;
using namespace term;
using namespace term::phone;

namespace
{
constexpr auto startTime = Milliseconds {1000};
constexpr auto pastTheLastInputWait = Milliseconds {2500};
constexpr auto pastTheStall = Milliseconds {4000};
constexpr auto pastTheRedialSilence = Milliseconds {9000};

PhoneSettings makeSettings(const std::string& explicitPaneId)
{
    auto settings = PhoneSettings {};
    settings.destination = {"jamie", "tamby"};
    settings.explicitPaneId = explicitPaneId;
    const auto scratchName = "cowterm-phone-connection-tests-" + std::to_string(::getpid());
    settings.storedScreenPath = std::filesystem::temp_directory_path() / scratchName / "last-screen.json";
    return settings;
}

std::string makeInfoLine(const std::string& paneId, const std::string& sessionName)
{
    return paneId + "\tzsh\t/tmp\t" + sessionName + "\n";
}

// A PhoneConnection whose host is a RecordingBridgeLink and whose clock the
// test moves by hand.
struct PhoneOnRecordedHost
{
    explicit PhoneOnRecordedHost(const std::string& explicitPaneId)
        : connection(makeSettings(explicitPaneId), factory.makeFactory(), [this] { return now; })
    {
        connection.onPaneChosen = [this](const ChosenPane& pane) { chosenPaneId = pane.paneId; };
        connection.onPaneStatus = [this](const std::string& status) { paneStatus = status; };
    }

    RecordingBridgeLink& getLink() { return factory.getLastLink(); }

    void bringLinkUp() { answer(bridge::makeHelloMessage("0.1.0")); }
    void answer(const std::string& message) { getLink().deliverBytes(bridge::encodeFrame(message)); }

    void answerInfo(const std::string& lines) { answer(proto::make("info", {}, lines)); }

    // The pane view's start, as TerminalView does with its shell.
    void openPaneView(const std::string& paneId)
    {
        paneShell = connection.makeShell(paneId);
        paneShell->start({}, [](const std::string&) {}, [] {});
    }

    std::vector<std::string> readSentMessages()
    {
        auto decoder = bridge::FrameDecoder {};
        return decoder.append(getLink().writtenBytes);
    }

    bool hasSentMessageStartingWith(const std::string& start)
    {
        const auto sent = readSentMessages();
        return std::ranges::any_of(sent, [&](const std::string& message) { return message.starts_with(start); });
    }

    RecordingLinkFactory factory;
    Milliseconds now = startTime;
    PhoneConnection connection;
    std::unique_ptr<Shell> paneShell;
    std::string chosenPaneId;
    std::string paneStatus;
};
} // namespace

auto listedExplicitPaneIsSpawned = test("PhoneConnection/listedExplicitPaneIsSpawned") = []
{
    auto phone = PhoneOnRecordedHost {"pane-1"};
    phone.openPaneView("pane-1");
    phone.bringLinkUp();
    check(!phone.hasSentMessageStartingWith("spawn pane-1 "));

    phone.answerInfo(makeInfoLine("pane-1", "tamby/x"));

    check(phone.hasSentMessageStartingWith("spawn pane-1 "));
    check(phone.chosenPaneId == "pane-1");
};

auto unlistedExplicitPaneIsNeverSpawned = test("PhoneConnection/unlistedExplicitPaneIsNeverSpawned") = []
{
    auto phone = PhoneOnRecordedHost {"pane-gone"};
    phone.openPaneView("pane-gone");
    phone.bringLinkUp();

    phone.answerInfo(makeInfoLine("pane-1", "tamby/x"));
    phone.answerInfo(makeInfoLine("pane-1", "tamby/x"));

    check(!phone.hasSentMessageStartingWith("spawn"));
    check(phone.paneStatus == "this pane no longer exists on tamby");
};

auto lastInputAnswerChoosesThePane = test("PhoneConnection/lastInputAnswerChoosesThePane") = []
{
    auto phone = PhoneOnRecordedHost {{}};
    phone.bringLinkUp();
    check(phone.hasSentMessageStartingWith("last-input"));

    phone.answerInfo(makeInfoLine("pane-a", "tamby/x") + makeInfoLine("pane-b", "tamby/x"));
    check(phone.chosenPaneId.empty());

    phone.answer("last-input pane-b 1794906370945");
    check(phone.chosenPaneId == "pane-b");
    check(phone.connection.getChosenPane()->sessionPaneIds == std::vector<std::string> {"pane-a", "pane-b"});
};

auto silentDaemonFallsBackToInfoAfterTwoSeconds = test("PhoneConnection/silentDaemonFallsBackToInfo") = []
{
    auto phone = PhoneOnRecordedHost {{}};
    phone.bringLinkUp();
    phone.answerInfo(makeInfoLine("pane-a", "tamby/x") + makeInfoLine("pane-b", "tamby/y"));

    phone.connection.handleTick();
    check(phone.chosenPaneId.empty());

    phone.now += pastTheLastInputWait;
    phone.connection.handleTick();
    check(phone.chosenPaneId == "pane-a");
};

auto quietPhoneLinkStallsThenRedials = test("PhoneConnection/quietLinkStallsThenRedials") = []
{
    auto phone = PhoneOnRecordedHost {"pane-1"};
    phone.bringLinkUp();
    phone.connection.handleTick();
    check(phone.connection.getLinkState() == PhoneLinkState::Live);

    phone.now += pastTheStall;
    phone.connection.handleTick();
    check(phone.connection.getLinkState() == PhoneLinkState::Stalled);
    check(phone.factory.requestedHosts.size() == 1);

    phone.now += pastTheRedialSilence;
    phone.connection.handleTick();
    check(phone.factory.requestedHosts.size() == 2);
    check(phone.connection.getLinkState() == PhoneLinkState::Connecting);
};
