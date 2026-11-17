#include <Emulator/TermState.h>
#include <Multiplexer/Protocol.h>
#include <PondApp/DataDirectory.h>

#include "DaemonConversation.h"

#include <NanoTest/NanoTest.h>
#include <eacp/Core/Core.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Network/IPC/Messenger.h>

#include <filesystem>
#include <optional>
#include <string>
#include <chrono>

using namespace nano;
using namespace eacp;
using namespace daemontest;

namespace
{
constexpr auto paneId = "restore-test-pane";
constexpr auto marker = "restore-me-42";
// sh and PowerShell both drop the empty quotes: the typed line never matches the marker.
constexpr auto echoMarkerCommand = "echo restore-me-\"\"42\r";
} // namespace

auto tPaneComesBackAfterDaemonRestart = test("DaemonRestore/paneComesBackAfterDaemonRestart") = []
{
    const auto instance = makeInstanceName("daemon-restore-test");

    {
        auto firstDaemon = launchDaemon(instance);
        auto first = DaemonConversation {instance};
        check(first.waitForConnection());

        first.messenger.send(term::proto::make("spawn", {paneId, "80", "24"}, "/tmp"));
        first.messenger.send(
            term::proto::make("write", {paneId}, echoMarkerCommand));
        check(first.waitForOutputContaining(marker));
        first.quitDaemon();
        firstDaemon.wait();
    }

    {
        auto secondDaemon = launchDaemon(instance);
        auto second = DaemonConversation {instance};
        check(second.waitForConnection());

        second.messenger.send(term::proto::make("spawn", {paneId, "80", "24"}, "/tmp"));
        const auto state = second.waitForState();
        check(state.has_value(), second.listReceivedVerbs());

        if (state)
        {
            check(hasLineContaining(state->screen, marker));
            check(!state->screen.modes.altScreen);
        }

        second.messenger.send(term::proto::make("kill", {paneId}));
        second.quitDaemon();
        secondDaemon.wait();
    }

    std::filesystem::remove_all(pondapp::getPondAppDataDirectory("CowTerm")
                                / ("panes-" + instance));
};
