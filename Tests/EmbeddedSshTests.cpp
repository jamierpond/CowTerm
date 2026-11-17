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
    check(!parseProbeArguments({"jamie@tamby", "--leave-pane", "a", "session", "extra"}).has_value());
};
