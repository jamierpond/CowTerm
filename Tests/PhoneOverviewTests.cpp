#include "PhoneOnRecordedHost.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term;
using namespace term::phone;
using namespace phonetest;

namespace
{
constexpr auto laptop = "jamie@tamby";
constexpr auto rack = "jamiepond@macprorack";
constexpr auto earlierTyping = 1000;
constexpr auto laterTyping = 2000;

PhoneSettings makeTwoHostSettings()
{
    auto settings = makeSettings({});
    settings.overviewDestinations = {{"jamie", "tamby"}, {"jamiepond", "macprorack"}};
    return settings;
}

// "<id>\t<last input>\t<last output>\t<state>\t<state since>\t<message>"
std::string makeActivityLine(const std::string& paneId,
                             int lastInput,
                             const std::string& state,
                             const std::string& message)
{
    return paneId + "\t" + std::to_string(lastInput) + "\t0\t" + state + "\t0\t" + message + "\n";
}

struct ShownOverview
{
    ShownOverview()
    {
        phone.bringLinkUpFor(laptop);
        phone.bringLinkUpFor(rack);
        phone.connection.getOverview().onRowsChanged = [this](const OverviewReport& report) { lastReport = report; };
        phone.connection.getOverview().setShown(true);
    }

    void answerLaptop(const std::string& infoLines, const std::string& activityLines)
    {
        phone.answerFrom(laptop, proto::make("info", {}, infoLines));
        phone.answerFrom(laptop, proto::make("activity", {}, activityLines));
    }

    const overview::OverviewRow& findRow(const std::string& sessionName) const
    {
        return *std::ranges::find(lastReport.sessions, sessionName, &overview::OverviewRow::sessionName);
    }

    PhoneOnRecordedHost phone {makeTwoHostSettings()};
    OverviewReport lastReport;
};
} // namespace

auto shownOverviewAsksEveryHost = test("PhoneOverview/shownOverviewAsksEveryHost") = []
{
    auto shown = ShownOverview {};

    check(shown.phone.hasSentTo(laptop, "activity"));
    check(shown.phone.hasSentTo(rack, "activity"));
    check(shown.lastReport.hostCount == 2);
};

auto rowsComeFromInfoAndActivity = test("PhoneOverview/rowsComeFromInfoAndActivity") = []
{
    auto shown = ShownOverview {};
    shown.answerLaptop(makeInfoLine("a", "tamby/CowTerm") + makeInfoLine("b", "tamby/Grazer"),
                       makeActivityLine("a", earlierTyping, "waiting", "Run the tests?")
                           + makeActivityLine("b", laterTyping, "", ""));

    check(shown.lastReport.sessions.size() == 2);
    check(shown.lastReport.sessions.front().sessionName == "tamby/CowTerm");
    check(shown.findRow("tamby/CowTerm").stateLine == "waiting: Run the tests?");
    check(shown.findRow("tamby/CowTerm").host == laptop);
};

auto olderDaemonGivesRowsWithoutAStateLine = test("PhoneOverview/olderDaemonGivesRowsWithoutAStateLine") = []
{
    auto shown = ShownOverview {};
    shown.phone.answerFrom(rack, proto::make("info", {}, makeInfoLine("c", "macprorack/Miro")));

    check(shown.lastReport.sessions.size() == 1);
    check(shown.findRow("macprorack/Miro").stateLine.empty());
};

auto unreachableHostIsReportedWithItsReason = test("PhoneOverview/unreachableHostIsReportedWithItsReason") = []
{
    constexpr auto reason = "unreachable (ssh: connect to host macprorack port 22: Operation timed out)";
    auto shown = ShownOverview {};
    shown.phone.factory.getLinkFor(rack).failTransport(reason);
    shown.phone.connection.getOverview().refresh();

    check(shown.lastReport.unreachableHosts.size() == 1);
    check(shown.lastReport.unreachableHosts.front().host == rack);
    check(shown.lastReport.unreachableHosts.front().reason == reason);
};

auto openingARowPicksThePaneTypedIntoLast = test("PhoneOverview/openingARowPicksThePaneTypedIntoLast") = []
{
    auto shown = ShownOverview {};
    shown.answerLaptop(makeInfoLine("a", "tamby/CowTerm") + makeInfoLine("b", "tamby/CowTerm"),
                       makeActivityLine("a", earlierTyping, "", "") + makeActivityLine("b", laterTyping, "", ""));

    const auto opened = shown.phone.connection.openSession(shown.findRow("tamby/CowTerm"));

    check(opened == std::optional<std::string> {"b"});
    check(shown.phone.connection.getChosenPane()->host == laptop);
    check(shown.phone.connection.getChosenPane()->sessionPaneIds.size() == 2);
};

auto rowWithoutActivityOpensItsFirstPane = test("PhoneOverview/rowWithoutActivityOpensItsFirstPane") = []
{
    auto shown = ShownOverview {};
    shown.phone.answerFrom(rack, proto::make("info", {}, makeInfoLine("c", "macprorack/Miro") + makeInfoLine("d", "macprorack/Miro")));

    check(shown.phone.connection.openSession(shown.findRow("macprorack/Miro")) == std::optional<std::string> {"c"});
};

auto rowIsFoundByHostAndSessionName = test("PhoneOverview/rowIsFoundByHostAndSessionName") = []
{
    auto shown = ShownOverview {};
    shown.phone.answerFrom(rack, proto::make("info", {}, makeInfoLine("c", "macprorack/Miro")));

    const auto found = findRowByName(shown.lastReport.sessions, "jamiepond@macprorack/macprorack/Miro");

    check(found.has_value());
    check(found->paneIds == std::vector<std::string> {"c"});
    check(!findRowByName(shown.lastReport.sessions, "jamie@tamby/macprorack/Miro").has_value());
};

auto rowWhosePanesAreGoneOpensNothing = test("PhoneOverview/rowWhosePanesAreGoneOpensNothing") = []
{
    auto shown = ShownOverview {};
    auto gone = overview::OverviewRow {};
    gone.host = laptop;
    gone.sessionName = "tamby/old";
    gone.paneIds = {"no-longer-listed"};

    check(!shown.phone.connection.openSession(gone).has_value());
};

auto openedPaneOnAnotherHostIsSpawnedThere = test("PhoneOverview/openedPaneOnAnotherHostIsSpawnedThere") = []
{
    auto shown = ShownOverview {};
    shown.phone.answerFrom(rack, proto::make("info", {}, makeInfoLine("c", "macprorack/Miro")));
    const auto opened = shown.phone.connection.openSession(shown.findRow("macprorack/Miro"));

    shown.phone.openPaneView(*opened);
    shown.phone.answerFrom(rack, proto::make("info", {}, makeInfoLine("c", "macprorack/Miro")));

    check(shown.phone.hasSentTo(rack, "spawn c "));
    check(!shown.phone.hasSentTo(laptop, "spawn"));
};

namespace
{
constexpr auto pastTheStall = Milliseconds {4000};
constexpr auto pastTheRedialSilence = Milliseconds {9000};

long countLinksMadeFor(const RecordingLinkFactory& factory, const std::string& host)
{
    return std::ranges::count(factory.requestedHosts, host);
}

// The overview's rack session opened on the pane screen.
void openRackSession(ShownOverview& shown)
{
    shown.phone.answerFrom(rack, proto::make("info", {}, makeInfoLine("c", "macprorack/Miro")));
    shown.phone.connection.openSession(shown.findRow("macprorack/Miro"));
    shown.phone.openPaneView("c");
}
} // namespace

auto linkStateFollowsTheShownPanesHost = test("PhoneLinkState/followsTheShownPanesHost") = []
{
    constexpr auto reason = "unreachable (ssh: connect to host macprorack port 22: Operation timed out)";
    auto shown = ShownOverview {};
    openRackSession(shown);
    shown.phone.connection.handleTick();
    check(shown.phone.connection.getLinkState() == PhoneLinkState::Live);

    shown.phone.factory.getLinkFor(rack).failTransport(reason);

    check(shown.phone.connection.getLinkState() == PhoneLinkState::Unreachable);
    check(shown.phone.connection.getFailureReason() == reason);
};

auto quietHostThatIsNotShownNeitherStallsNorRedials = test("PhoneLinkState/quietHostThatIsNotShownNeitherStallsNorRedials") = []
{
    auto phone = PhoneOnRecordedHost {makeTwoHostSettings()};
    phone.bringLinkUpFor(laptop);
    phone.bringLinkUpFor(rack);

    phone.now += pastTheRedialSilence;
    phone.answerFrom(laptop, proto::make("info", {}, makeInfoLine("a", "tamby/CowTerm")));
    phone.connection.handleTick();

    check(phone.connection.getLinkState() == PhoneLinkState::Live);
    check(countLinksMadeFor(phone.factory, rack) == 1);
};

auto quietShownHostStallsThenRedials = test("PhoneLinkState/quietShownHostStallsThenRedials") = []
{
    auto shown = ShownOverview {};
    openRackSession(shown);

    shown.phone.now += pastTheStall;
    shown.phone.answerFrom(laptop, proto::make("info", {}, makeInfoLine("a", "tamby/CowTerm")));
    shown.phone.connection.handleTick();
    check(shown.phone.connection.getLinkState() == PhoneLinkState::Stalled);

    shown.phone.now += pastTheRedialSilence;
    shown.phone.answerFrom(laptop, proto::make("info", {}, makeInfoLine("a", "tamby/CowTerm")));
    shown.phone.connection.handleTick();
    check(countLinksMadeFor(shown.phone.factory, rack) == 2);
    check(countLinksMadeFor(shown.phone.factory, laptop) == 1);
};
