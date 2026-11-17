#include "PhoneOnRecordedHost.h"

#include "Emulator/TermState.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term;
using namespace term::phone;
using namespace phonetest;

namespace
{
constexpr auto pastTheLastInputWait = Milliseconds {2500};
constexpr auto pastTheStall = Milliseconds {4000};
constexpr auto pastTheRedialSilence = Milliseconds {9000};
} // namespace

auto listedExplicitPaneIsSpawned = test("PhoneConnection/listedExplicitPaneIsSpawned") = []
{
    auto phone = PhoneOnRecordedHost {makeSettings("pane-1")};
    phone.openPaneView("pane-1");
    phone.bringLinkUp();
    check(!phone.hasSentMessageStartingWith("spawn pane-1 "));

    phone.answerInfo(makeInfoLine("pane-1", "tamby/x"));

    check(phone.hasSentMessageStartingWith("spawn pane-1 "));
    check(phone.chosenPaneId == "pane-1");
};

auto unlistedExplicitPaneIsNeverSpawned = test("PhoneConnection/unlistedExplicitPaneIsNeverSpawned") = []
{
    auto phone = PhoneOnRecordedHost {makeSettings("pane-gone")};
    phone.openPaneView("pane-gone");
    phone.bringLinkUp();

    phone.answerInfo(makeInfoLine("pane-1", "tamby/x"));
    phone.answerInfo(makeInfoLine("pane-1", "tamby/x"));

    check(!phone.hasSentMessageStartingWith("spawn"));
    check(phone.paneStatus == "this pane no longer exists on tamby");
};

auto lastInputAnswerChoosesThePane = test("PhoneConnection/lastInputAnswerChoosesThePane") = []
{
    auto phone = PhoneOnRecordedHost {makeSettings({})};
    phone.bringLinkUp();
    check(phone.hasSentMessageStartingWith("last-input"));

    phone.answerInfo(makeInfoLine("pane-a", "tamby/x") + makeInfoLine("pane-b", "tamby/x"));
    check(phone.chosenPaneId.empty());

    phone.answer("last-input pane-b 1794906370945");
    check(phone.chosenPaneId == "pane-b");
    check(phone.connection.getChosenPane()->sessionPaneIds == std::vector<std::string> {"pane-a", "pane-b"});
};

auto silentDaemonOpensNoPane = test("PhoneConnection/silentDaemonOpensNoPane") = []
{
    auto phone = PhoneOnRecordedHost {makeSettings({})};
    phone.bringLinkUp();
    phone.answerInfo(makeInfoLine("pane-a", "tamby/x") + makeInfoLine("pane-b", "tamby/y"));

    phone.now += pastTheLastInputWait;
    phone.connection.handleTick();
    check(phone.chosenPaneId.empty(),
          "opening a pane takes its size, so the overview stays");
};

auto silentDaemonOpensThePaneShownLastTime =
    test("PhoneConnection/silentDaemonOpensThePaneShownLastTime") = []
{
    auto phone = PhoneOnRecordedHost {makeSettings({})};
    phone.connection.setPaneShownLastTime({phone.connection.getHost(), "pane-b"});
    phone.bringLinkUp();
    phone.answerInfo(makeInfoLine("pane-a", "tamby/x")
                     + makeInfoLine("pane-b", "tamby/y"));

    phone.connection.handleTick();
    check(phone.chosenPaneId.empty(), "waits for a last-input answer first");

    phone.now += pastTheLastInputWait;
    phone.connection.handleTick();
    check(phone.chosenPaneId == "pane-b");
};

auto unnamedPanesFormASessionByFolder =
    test("PhoneConnection/unnamedPanesFormASessionByFolder") = []
{
    auto phone = PhoneOnRecordedHost {makeSettings({})};
    phone.bringLinkUp();
    phone.answerInfo(
        "pane-a\tzsh\t/Users/jamiepond/projects/CowTerm\npane-b\tclaude\t/Users/jamiepond/projects/CowTerm\n");
    phone.answer("last-input pane-b 1794906370945");

    const auto& chosen = phone.connection.getChosenPane();
    check(chosen && chosen->sessionName == "CowTerm");
    check(chosen
          && chosen->sessionPaneIds
                 == std::vector<std::string> {"pane-a", "pane-b"});
};

auto backgroundGivesAProtocolFourPaneItsSizeBack =
    test("PhoneConnection/backgroundGivesAProtocolFourPaneItsSizeBack") = []
{
    constexpr auto deskColumns = 150;
    constexpr auto deskRows = 40;
    auto phone = PhoneOnRecordedHost {makeSettings("pane-1")};
    phone.openPaneView("pane-1");
    phone.bringLinkUp();
    phone.answer(proto::make("daemon-hello",
                             {std::to_string(proto::sizeOwnerProtocolVersion - 1)}));
    phone.answerInfo(makeInfoLine("pane-1", "tamby/x"));
    auto state = TerminalState {};
    state.screen.columns = deskColumns;
    state.screen.rows = deskRows;
    phone.answer(proto::make("state", {"pane-1"}, serializeTerminalState(state)));

    auto returnedHosts = std::vector<std::string> {};
    phone.connection.onSizesReturned =
        [&](const std::string& host, const std::vector<DaemonClient::ReturnedSize>&)
    { returnedHosts.push_back(host); };
    phone.connection.handleBackground();

    check(phone.hasSentMessageStartingWith(proto::make(
        "resize",
        {"pane-1", std::to_string(deskColumns), std::to_string(deskRows)})));
    check(returnedHosts == std::vector<std::string> {phone.connection.getHost()});
};
auto quietHostIsNotRedialledWhileNoPaneIsShown = test("PhoneConnection/quietHostIsNotRedialledWhileNoPaneIsShown") = []
{
    auto phone = PhoneOnRecordedHost {makeSettings({})};
    phone.bringLinkUp();

    phone.now += pastTheRedialSilence;
    phone.connection.handleTick();
    check(phone.factory.requestedHosts.size() == 1, "the overview asks no host every second");
};

auto quietPhoneLinkStallsThenRedials =
    test("PhoneConnection/quietLinkStallsThenRedials") = []
{
    auto phone = PhoneOnRecordedHost {makeSettings("pane-1")};
    phone.openPaneView("pane-1");
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
