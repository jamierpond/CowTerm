#include "PhoneOnRecordedHost.h"

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

auto silentDaemonFallsBackToInfoAfterTwoSeconds = test("PhoneConnection/silentDaemonFallsBackToInfo") = []
{
    auto phone = PhoneOnRecordedHost {makeSettings({})};
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
    auto phone = PhoneOnRecordedHost {makeSettings("pane-1")};
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
