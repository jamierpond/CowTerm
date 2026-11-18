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

auto paneHeldWhenKilledGetsItsSizeBackNextLaunch =
    test("PhoneConnection/paneHeldWhenKilledGetsItsSizeBackNextLaunch") = []
{
    constexpr auto deskColumns = 150;
    constexpr auto deskRows = 41;
    const auto settings = makeSettings({});
    const auto heldPanePath = settings.storedScreenPath.parent_path() / "held-pane.json";
    saveHeldPane(heldPanePath, {"jamie@tamby", "pane-held", {deskColumns, deskRows}});

    auto phone = PhoneOnRecordedHost {settings};
    phone.bringLinkUp();
    phone.answer(proto::make("daemon-hello", {std::to_string(proto::sizeOwnerProtocolVersion - 1)}));

    check(phone.hasSentMessageStartingWith(
        proto::make("resize", {"pane-held", std::to_string(deskColumns), std::to_string(deskRows)})));
    check(!loadHeldPane(heldPanePath), "given back once");
};

auto scriptedLaunchOpensNoPaneOnItsOwn = test("PhoneConnection/aScriptedLaunchNeverOpensAPaneOnItsOwn") = []
{
    auto settings = makeSettings({});
    settings.choosesPaneAtLaunch = false;
    auto phone = PhoneOnRecordedHost {settings};
    phone.bringLinkUp();
    phone.answerInfo(makeInfoLine("pane-a", "tamby/x"));

    phone.answer("last-input pane-a 1794906370945");
    check(phone.chosenPaneId.empty(), "a test must not take the size of the pane someone typed in");
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

namespace
{
std::filesystem::path findTemporaryPanesPath(const PhoneSettings& settings)
{
    return settings.storedScreenPath.parent_path() / "temporary-panes.json";
}

// pane-1 on screen, listed in the folder makeInfoLine gives every pane.
struct PhoneShowingAPane
{
    PhoneShowingAPane()
    {
        std::filesystem::remove(findTemporaryPanesPath(settings));
        phone.openPaneView("pane-1");
        phone.bringLinkUp();
        phone.answerInfo(makeInfoLine("pane-1", "tamby/x"));
    }

    PhoneSettings settings = makeSettings("pane-1");
    PhoneOnRecordedHost phone {settings};
};

constexpr auto shownPanesFolder = "/tmp";
constexpr auto randomNumberOfAnEarlierLaunch = std::uint64_t {42};
} // namespace

auto diffViewOpensInTheShownPanesFolder = test("PhoneConnection/diffViewOpensInTheShownPanesFolder") = []
{
    auto showing = PhoneShowingAPane {};
    const auto opening = showing.phone.connection.openDiffView();

    check(opening.pane && opening.pane->workingDirectory == shownPanesFolder);
    check(opening.pane && opening.pane->host == showing.phone.connection.getHost());
    check(loadTemporaryPanes(findTemporaryPanesPath(showing.settings)).size() == 1,
          "the record is on disk before dv starts");

    auto shell = showing.phone.connection.makeDiffViewShell(*opening.pane);
    shell->start({{}, shownPanesFolder, diffViewCommand, {}}, [](const std::string&) {}, [] {});

    const auto spawn = proto::make("spawn", {opening.pane->paneId});
    const auto sent = showing.phone.readSentMessages(showing.phone.connection.getHost());
    const auto found = std::ranges::find_if(sent, [&](const std::string& message) { return message.starts_with(spawn); });
    check(found != sent.end());
    check(found != sent.end() && proto::parseSpawnPayload(proto::parse(*found).payload).command == diffViewCommand);
};

auto closingDiffViewKillsItsPane = test("PhoneConnection/closingDiffViewKillsItsPane") = []
{
    auto showing = PhoneShowingAPane {};
    const auto pane = *showing.phone.connection.openDiffView().pane;

    showing.phone.connection.closeDiffView(pane);

    check(showing.phone.hasSentMessageStartingWith(proto::make("kill", {pane.paneId})));
    check(loadTemporaryPanes(findTemporaryPanesPath(showing.settings)).empty());
};

auto diffViewWaitsForThePanesFolder = test("PhoneConnection/diffViewWaitsForThePanesFolder") = []
{
    auto phone = PhoneOnRecordedHost {makeSettings("pane-1")};
    phone.openPaneView("pane-1");

    const auto opening = phone.connection.openDiffView();
    check(!opening.pane);
    check(!opening.whyNot.empty());
};

auto diffViewPaneIsNotOneOfTheSessionsPanes = test("PhoneConnection/diffViewPaneIsNotOneOfTheSessionsPanes") = []
{
    auto showing = PhoneShowingAPane {};
    const auto pane = *showing.phone.connection.openDiffView().pane;

    showing.phone.answerInfo(makeInfoLine("pane-1", "tamby/x") + makeInfoLine(pane.paneId, "tamby/x"));

    check(showing.phone.connection.getChosenPane()->sessionPaneIds == std::vector<std::string> {"pane-1"});
};

auto diffViewLeftByAKilledAppIsKilledNextLaunch =
    test("PhoneConnection/diffViewLeftByAKilledAppIsKilledNextLaunch") = []
{
    const auto settings = makeSettings({});
    const auto path = findTemporaryPanesPath(settings);
    std::filesystem::remove(path);
    const auto leftBehind = TemporaryPane {"jamie@tamby", makeDiffViewPaneId(randomNumberOfAnEarlierLaunch)};
    addTemporaryPane(path, leftBehind);

    auto phone = PhoneOnRecordedHost {settings};
    phone.bringLinkUp();
    check(!phone.hasSentMessageStartingWith("kill"), "not before the daemon answers");

    phone.answer(proto::make("daemon-hello", {std::to_string(proto::sizeOwnerProtocolVersion - 1)}));

    check(phone.hasSentMessageStartingWith(proto::make("kill", {leftBehind.paneId})));
    check(loadTemporaryPanes(path).empty());
};

auto diffViewSaysWhenItIsNotInstalled = test("PhoneConnection/diffViewSaysWhenItIsNotInstalled") = []
{
    auto showing = PhoneShowingAPane {};
    const auto pane = *showing.phone.connection.openDiffView().pane;
    auto missingProgram = std::string {};
    auto shell = showing.phone.connection.makeDiffViewShell(pane);
    auto searchedPath = std::string {};
    shell->onCommandNotInstalled = [&](const MissingCommand& missing)
    {
        missingProgram = missing.program;
        searchedPath = missing.searchedPath;
    };
    shell->start({{}, shownPanesFolder, diffViewCommand, {}}, [](const std::string&) {}, [] {});

    showing.phone.answer(
        proto::make("missing-command", {pane.paneId, proto::encodeArgument("/usr/bin:/my bin")}, diffViewCommand));

    check(missingProgram == diffViewCommand);
    check(searchedPath == "/usr/bin:/my bin");
};

namespace
{
PhoneOnRecordedHost& showPaneOne(PhoneOnRecordedHost& phone)
{
    phone.openPaneView("pane-1");
    phone.bringLinkUp();
    phone.answerInfo(makeInfoLine("pane-1", "tamby/x"));
    return phone;
}
} // namespace

auto paneScreenViewsTheChosenPane = test("PhoneConnection/paneScreenInTheForegroundViewsTheChosenPane") = []
{
    auto phone = PhoneOnRecordedHost {makeSettings("pane-1")};
    showPaneOne(phone);
    check(!phone.hasSentMessageStartingWith("viewing"), "the pane screen is not up yet");

    phone.connection.setPaneScreenShown(true);
    check(phone.hasSentMessageStartingWith("viewing pane-1"));

    phone.connection.setPaneScreenShown(false);
    check(phone.hasSentMessageStartingWith("not-viewing pane-1"));
    check(!phone.connection.getViewedPane().has_value());
};

auto backgroundStopsViewing = test("PhoneConnection/backgroundStopsViewingAndWakingViewsAgain") = []
{
    auto phone = PhoneOnRecordedHost {makeSettings("pane-1")};
    showPaneOne(phone);
    phone.connection.setPaneScreenShown(true);

    phone.connection.handleBackground();
    check(phone.hasSentMessageStartingWith("not-viewing pane-1"));
    check(!phone.connection.getViewedPane().has_value());

    phone.connection.handleWake();
    check(phone.connection.getViewedPane().has_value());
};

auto anotherPaneOfTheSessionIsViewedInstead = test("PhoneConnection/choosingAnotherPaneViewsItInstead") = []
{
    auto phone = PhoneOnRecordedHost {makeSettings("pane-1")};
    phone.openPaneView("pane-1");
    phone.bringLinkUp();
    phone.answerInfo(makeInfoLine("pane-1", "tamby/x") + makeInfoLine("pane-2", "tamby/x"));
    phone.connection.setPaneScreenShown(true);

    check(phone.connection.choosePaneOfSession("pane-2"));

    check(phone.hasSentMessageStartingWith("not-viewing pane-1"));
    check(phone.hasSentMessageStartingWith("viewing pane-2"));
};
