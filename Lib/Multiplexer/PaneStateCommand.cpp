#include "PaneStateCommand.h"

#include "Protocol.h"

#include <eacp/Core/App/App.h>
#include <eacp/Network/IPC/Messenger.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <string_view>

namespace term::cli
{
using namespace eacp;

namespace
{
constexpr auto knownStates = std::array<std::string_view, 3> {"working", "waiting", "finished"};
constexpr auto dialTimeout = Time::MS {1500};
constexpr int exitOk = 0;
constexpr int exitUsage = 2;
constexpr int exitDaemonUnreachable = 4;

bool isKnownState(const std::string& state)
{
    return std::ranges::find(knownStates, state) != knownStates.end();
}

// Sends the report, then a hello: the daemon answers messages in order, so its
// hello reply means the report has been handled.
struct PaneStateSender
{
    explicit PaneStateSender(PaneStateReport reportToSend)
        : report(std::move(reportToSend))
    {
        Apps::setDockIconVisible(false);

        messenger->onConnected = [this]
        {
            messenger->send(proto::make("pane-state", {report.paneId, report.state}, report.message));
            messenger->send(proto::make("hello"));
        };

        messenger->onMessage = [this](const std::string&) { finish(exitOk); };
        messenger->onDisconnected = [this] { finish(exitDaemonUnreachable); };
    }

    void finish(int exitCode)
    {
        if (finished)
            return;

        finished = true;
        Apps::quit(exitCode);
    }

    PaneStateReport report;
    bool finished = false;
    std::unique_ptr<IPC::Messenger> messenger =
        std::make_unique<IPC::Messenger>(proto::serverName(), dialTimeout);
};
} // namespace

ParsedPaneStateReport parsePaneStateArguments(const std::vector<std::string>& args,
                                              const std::string& defaultPaneId)
{
    auto report = PaneStateReport {defaultPaneId, {}, {}};

    for (auto index = std::size_t {0}; index < args.size(); ++index)
    {
        const auto hasValue = index + 1 < args.size();

        if (args[index] == "--message" && hasValue)
            report.message = args[++index];
        else if (args[index] == "--pane" && hasValue)
            report.paneId = args[++index];
        else if (report.state.empty())
            report.state = args[index];
        else
            return {std::nullopt, "unexpected argument: " + args[index]};
    }

    if (!isKnownState(report.state))
        return {std::nullopt, "the state is one of working, waiting, finished"};

    if (report.paneId.empty())
        return {std::nullopt, "no pane: run it inside a CowTerm pane or pass --pane <id>"};

    return {report, {}};
}

int runPaneStateCommand(const std::vector<std::string>& args, const std::string& defaultPaneId)
{
    const auto parsed = parsePaneStateArguments(args, defaultPaneId);

    if (!parsed.report)
    {
        std::fprintf(stderr, "ct pane-state: %s (see ct help)\n", parsed.usageError.c_str());
        return exitUsage;
    }

    return Apps::run<PaneStateSender>(*parsed.report);
}
} // namespace term::cli
