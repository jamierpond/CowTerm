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
    check(!arguments->isAdoptingAPane);
};

auto probeAdoptsTheNamedPane = test("EmbeddedSshProbe/adoptsTheNamedPane") = []
{
    const auto arguments = parseProbeArguments({"jamie@tamby", "--adopt", "pane-7"});

    check(arguments.has_value());
    check(arguments->isAdoptingAPane);
    check(arguments->adoptedPaneId == "pane-7");
};

auto probeAdoptsTheFirstPaneWithoutAnId = test("EmbeddedSshProbe/adoptsTheFirstPaneWithoutAnId") = []
{
    const auto arguments = parseProbeArguments({"jamie@tamby", "--adopt"});

    check(arguments.has_value());
    check(arguments->isAdoptingAPane);
    check(arguments->adoptedPaneId.empty());
};

auto probeRefusesOtherArguments = test("EmbeddedSshProbe/refusesOtherArguments") = []
{
    check(!parseProbeArguments({}).has_value());
    check(!parseProbeArguments({"tamby"}).has_value());
    check(!parseProbeArguments({"jamie@tamby", "pane-7"}).has_value());
    check(!parseProbeArguments({"jamie@tamby", "--adopt", "pane-7", "extra"}).has_value());
};
