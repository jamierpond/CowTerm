#include "EmbeddedSshProbeArguments.h"

#include "Multiplexer/EmbeddedSshLink.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term;

auto destinationWithoutPortUsesTwentyTwo = test("EmbeddedSsh/destinationWithoutPortUsesTwentyTwo") = []
{
    const auto destination = parseSshDestination("jamiepond@macprorack");

    check(destination.has_value());
    check(destination->user == "jamiepond");
    check(destination->host == "macprorack");
    check(destination->port == defaultSshPort);
};

auto destinationTakesAPort = test("EmbeddedSsh/destinationTakesAPort") = []
{
    constexpr auto expectedPort = 2222;
    const auto destination = parseSshDestination("jamie@tamby:2222");

    check(destination.has_value());
    check(destination->host == "tamby");
    check(destination->port == expectedPort);
};

auto destinationNamesTheMachineAndDialsItsAddress = test("EmbeddedSsh/destinationNamesTheMachineAndDialsItsAddress") = []
{
    constexpr auto expectedPort = 2222;
    const auto renamed = parseSshDestination("jamiepond@tamby=jamies-mac-mini.tail8acf23.ts.net:2222");

    check(renamed.has_value());
    check(renamed->user == "jamiepond");
    check(renamed->host == "tamby", "sessions and colours keep the machine's name");
    check(renamed->address == "jamies-mac-mini.tail8acf23.ts.net");
    check(renamed->port == expectedPort);
    check(getAddressToDial(*renamed) == "jamies-mac-mini.tail8acf23.ts.net");

    const auto plain = parseSshDestination("jamiepond@macprorack");
    check(plain && getAddressToDial(*plain) == "macprorack", "a pairing from before addresses dials the name");

    check(!parseSshDestination("jamiepond@tamby=").has_value());
    check(!parseSshDestination("jamiepond@=jamies-mac-mini.tail8acf23.ts.net").has_value());
    check(!parseSshDestination("jamiepond@tamby=jamies-mac-mini:ssh").has_value());
};

auto destinationNeedsAUserAndAHost = test("EmbeddedSsh/destinationNeedsAUserAndAHost") = []
{
    check(!parseSshDestination("tamby").has_value());
    check(!parseSshDestination("@tamby").has_value());
    check(!parseSshDestination("jamie@").has_value());
};

auto destinationRefusesABadPort = test("EmbeddedSsh/destinationRefusesABadPort") = []
{
    check(!parseSshDestination("jamie@tamby:").has_value());
    check(!parseSshDestination("jamie@tamby:ssh").has_value());
    check(!parseSshDestination("jamie@tamby:70000").has_value());
};

auto probeOpensAThrowawayPaneByDefault = test("EmbeddedSshProbe/opensAThrowawayPaneByDefault") = []
{
    const auto arguments = parseProbeArguments({"jamie@tamby"});

    check(arguments.has_value());
    check(arguments->mode == ProbeMode::ThrowawayPane);
};

auto probeAdoptsTheNamedPane = test("EmbeddedSshProbe/adoptsTheNamedPane") = []
{
    const auto arguments = parseProbeArguments({"jamie@tamby", "--adopt", "pane-7"});

    check(arguments.has_value());
    check(arguments->mode == ProbeMode::AdoptPane);
    check(arguments->paneId == "pane-7");
};

auto probeAdoptsTheFirstPaneWithoutAnId = test("EmbeddedSshProbe/adoptsTheFirstPaneWithoutAnId") = []
{
    const auto arguments = parseProbeArguments({"jamie@tamby", "--adopt"});

    check(arguments.has_value());
    check(arguments->mode == ProbeMode::AdoptPane);
    check(arguments->paneId.empty());
};

auto probeLeavesAndKillsANamedPane = test("EmbeddedSshProbe/leavesAndKillsANamedPane") = []
{
    const auto leave = parseProbeArguments({"jamie@tamby", "--leave-pane", "phone-test"});
    const auto kill = parseProbeArguments({"jamie@tamby", "--kill-pane", "phone-test"});

    check(leave.has_value());
    check(leave->mode == ProbeMode::LeavePaneRunning);
    check(leave->paneId == "phone-test");
    check(kill.has_value());
    check(kill->mode == ProbeMode::KillPane);
    check(!parseProbeArguments({"jamie@tamby", "--kill-pane"}).has_value());
};

auto probeLeavesAPaneInANamedSession = test("EmbeddedSshProbe/leavesAPaneInANamedSession") = []
{
    const auto leave = parseProbeArguments({"jamie@tamby", "--leave-pane", "phone-two-panes-a", "phone-two-panes"});

    check(leave.has_value());
    check(leave->sessionName == "phone-two-panes");
};

auto probeLeavesAPaneInAFolderAndListsPanes = test("EmbeddedSshProbe/leavesAPaneInAFolderAndListsPanes") = []
{
    const auto leave =
        parseProbeArguments({"jamie@tamby", "--leave-pane", "phone-dv-test", "phone-dv-test", "/tmp/cowterm-dv-test"});
    const auto list = parseProbeArguments({"jamie@tamby", "--list-panes"});

    check(leave.has_value() && leave->folder == "/tmp/cowterm-dv-test");
    check(list.has_value() && list->mode == ProbeMode::ListPanes);
    check(!parseProbeArguments({"jamie@tamby", "--kill-pane", "phone-dv-test", "session", "/tmp"}).has_value());
};

auto probeListsLayouts = test("EmbeddedSshProbe/listsLayouts") = []
{
    const auto layouts = parseProbeArguments({"jamie@tamby", "--list-layouts"});

    check(layouts.has_value() && layouts->mode == ProbeMode::ListLayouts);
};

auto probeListsUnread = test("EmbeddedSshProbe/listsUnread") = []
{
    const auto unread = parseProbeArguments({"jamie@tamby", "--list-unread"});

    check(unread.has_value() && unread->mode == ProbeMode::ListUnread);
};

auto probeWatchesAPaneAndQuitsADaemon = test("EmbeddedSshProbe/watchesAPaneAndQuitsADaemon") = []
{
    const auto watch = parseProbeArguments({"jamie@tamby", "--watch-pane", "phone-test"});
    const auto quit = parseProbeArguments({"jamie@tamby", "--quit-server"});

    check(watch.has_value());
    check(watch->mode == ProbeMode::WatchPane);
    check(quit.has_value());
    check(quit->mode == ProbeMode::QuitServer);
    check(!parseProbeArguments({"jamie@tamby", "--watch-pane"}).has_value());
};

auto probeRefusesOtherArguments = test("EmbeddedSshProbe/refusesOtherArguments") = []
{
    check(!parseProbeArguments({}).has_value());
    check(!parseProbeArguments({"tamby"}).has_value());
    check(!parseProbeArguments({"jamie@tamby", "pane-7"}).has_value());
    check(!parseProbeArguments({"jamie@tamby", "--adopt", "pane-7", "extra"}).has_value());
    check(!parseProbeArguments({"jamie@tamby", "--leave-pane", "a", "session", "/tmp", "extra"}).has_value());
};
