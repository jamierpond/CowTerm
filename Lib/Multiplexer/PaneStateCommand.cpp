#include "PaneStateCommand.h"
#include "ClaudeHookReport.h"

#include "Protocol.h"

#include <eacp/Core/App/App.h>
#include <eacp/Network/IPC/Messenger.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <iostream>
#include <iterator>
#include <memory>
#include <string_view>

namespace term::cli
{
using namespace eacp;

namespace
{
constexpr auto knownStates =
    std::array {std::string_view {"working"}, std::string_view {"waiting"}, std::string_view {"finished"}};
constexpr auto dialTimeout = Time::MS {1500};
constexpr int exitOk = 0;
constexpr int exitUsage = 2;
constexpr int exitDaemonUnreachable = 4;
constexpr auto claudeHookFlag = std::string_view {"--claude-hook"};

std::string readStandardInput()
{
    return {std::istreambuf_iterator<char> {std::cin}, std::istreambuf_iterator<char> {}};
}

struct ValueOption
{
    std::string_view flag;
    std::string PaneStateReport::*field;
};

const auto valueOptions = std::array {ValueOption {"--message", &PaneStateReport::message},
                                      ValueOption {"--pane", &PaneStateReport::paneId}};

bool isKnownState(const std::string& state)
{
    return std::ranges::find(knownStates, state) != knownStates.end();
}

std::optional<std::string> findProblemWithReport(const PaneStateReport& report)
{
    if (!isKnownState(report.state))
        return "the state is one of working, waiting, finished";

    if (report.paneId.empty())
        return "no pane: run it inside a CowTerm pane or pass --pane <id>";

    return std::nullopt;
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

namespace
{
ParsedPaneStateReport findProblemWithClaudeHookArguments(const PaneStateReport& report)
{
    if (!report.state.empty())
        return {std::nullopt, "--claude-hook reads the state from Claude Code; give no state", true};

    if (report.paneId.empty())
        return {std::nullopt, "no pane: run it inside a CowTerm pane or pass --pane <id>", true};

    return {report, {}, true};
}
} // namespace

ParsedPaneStateReport parsePaneStateArguments(const std::vector<std::string>& args,
                                              const std::string& defaultPaneId)
{
    auto report = PaneStateReport {defaultPaneId, {}, {}};
    auto readsClaudeHookInput = false;

    for (auto index = std::size_t {0}; index < args.size(); ++index)
    {
        if (args[index] == claudeHookFlag)
        {
            readsClaudeHookInput = true;
            continue;
        }

        const auto option = std::ranges::find(valueOptions, args[index], &ValueOption::flag);
        const auto hasValue = index + 1 < args.size();

        if (option != valueOptions.end() && hasValue)
        {
            report.*(option->field) = args[++index];
            continue;
        }

        if (!report.state.empty())
            return {std::nullopt, "unexpected argument: " + args[index]};

        report.state = args[index];
    }

    if (readsClaudeHookInput)
        return findProblemWithClaudeHookArguments(report);

    if (const auto problem = findProblemWithReport(report))
        return {std::nullopt, *problem};

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

    if (!parsed.readsClaudeHookInput)
        return Apps::run<PaneStateSender>(*parsed.report);

    const auto hookReport = readClaudeHookReport(readStandardInput(), parsed.report->paneId);

    if (!hookReport)
        return exitOk;

    return Apps::run<PaneStateSender>(*hookReport);
}
} // namespace term::cli
