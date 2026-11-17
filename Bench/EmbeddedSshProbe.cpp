// Not a test: proves the in-process ssh link (EmbeddedSshLink) reaches a
// host's session daemon. It lists the host's panes, opens a throwaway pane,
// runs an echo there, prints that pane's screen as the daemon's `state` message
// draws it, then kills the pane. With --adopt it prints an existing pane's
// screen instead and releases it, never writing to it. --leave-pane opens a
// pane running `seq 1 300` and leaves it for the phone to open; --kill-pane
// ends it. Run by scripts/probe-embedded-ssh.sh and run-ios-against-host.sh.

#include "EmbeddedSshProbeArguments.h"

#include "Emulator/TerminalModel.h"
#include "Multiplexer/Protocol.h"
#include "TerminalView/CellText.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

using namespace eacp;

namespace
{
constexpr auto waitForTheWholeProbe = Time::MS {60000};
constexpr auto probeColumns = 80;
constexpr auto probeRows = 24;
constexpr auto probeSession = "embedded-ssh-probe";
constexpr auto probeDirectory = "/tmp";
constexpr auto marker = "hello-from-embedded-ssh";
constexpr auto lastNumberOfSequence = "300";

// What the probe types into its pane, and the line that shows it ran.
struct PaneScript
{
    std::string command;
    std::string lineThatShowsItRan;
};

PaneScript makePaneScript(ProbeMode mode)
{
    if (mode == ProbeMode::LeavePaneRunning)
        return {std::string {"seq 1 "} + lastNumberOfSequence + "\r", lastNumberOfSequence};

    return {std::string {"echo "} + marker + "\r", marker};
}
constexpr auto failedExitCode = 1;
constexpr auto usageExitCode = 2;

// info lines: id, foreground process, working directory, session.
constexpr auto infoSessionField = std::size_t {3};

struct ListedPane
{
    std::string id;
    std::string session;
    std::string infoLine;
};

std::vector<ListedPane> readListedPanes(const std::string& infoPayload)
{
    auto panes = std::vector<ListedPane> {};

    for (const auto& line: Strings::split(infoPayload, '\n'))
    {
        const auto fields = Strings::split(line, '\t');

        if (fields.size() < infoSessionField)
            continue;

        auto session = fields.size() > infoSessionField ? fields[infoSessionField] : std::string {};
        panes.push_back({fields[0], std::move(session), Strings::join(fields, "  ")});
    }

    return panes;
}

std::string makeThrowawayPaneId()
{
    return "embedded-ssh-probe-" + std::to_string(::getpid());
}

std::string readRowText(const term::TermScreen& screen, int row)
{
    return term::extractVisibleRowText(screen, screen.lineAt(row, 0), screen.columns());
}

// Every row down to the last one with text on it.
void printScreen(const term::TermScreen& screen)
{
    auto lastRowWithText = -1;

    for (auto row = 0; row < screen.rows(); ++row)
    {
        if (!readRowText(screen, row).empty())
            lastRowWithText = row;
    }

    for (auto row = 0; row <= lastRowWithText; ++row)
        std::printf("  | %s\n", readRowText(screen, row).c_str());
}

bool isShowingLine(const term::TermScreen& screen, const std::string& line)
{
    for (auto row = 0; row < screen.rows(); ++row)
    {
        if (readRowText(screen, row) == line)
            return true;
    }

    return false;
}

// Drives one probe over the link: every daemon message arrives in handleMessage.
class EmbeddedSshProbe
{
public:
    EmbeddedSshProbe(const ProbeArguments& argumentsToUse, const term::SshCredentials& credentials)
        : arguments(argumentsToUse)
        , link(arguments.destination, credentials)
    {
        link.onReady = [this] { handleReady(); };
        link.onFailed = [this](const std::string& reason) { finish("link failed: " + reason); };
        link.onMessage = [this](const std::string& body) { handleMessage(body); };
    }

    int run()
    {
        Threads::runEventLoopUntil([this] { return isFinished; }, waitForTheWholeProbe);

        if (!isFinished)
            finish("timed out waiting for the daemon");

        if (!failure.empty())
            std::fprintf(stderr, "%s\n", failure.c_str());

        return failure.empty() ? 0 : failedExitCode;
    }

private:
    void handleReady()
    {
        std::printf("connected to %s through EmbeddedSshLink; bridge hello received\n",
                    arguments.destination.host.c_str());

        if (arguments.mode == ProbeMode::KillPane)
            return killPane(arguments.paneId);

        link.send(term::proto::make("info"));
    }

    void handleMessage(const std::string& body)
    {
        const auto message = term::proto::parse(body);

        if (message.verb == "info")
            return handleInfo(message.payload);

        if (message.args.empty() || message.args[0] != paneId)
            return;

        if (message.verb == "output")
            handleOutput(message.payload);
        else if (message.verb == "state")
            handleState(message.payload);
        else if (message.verb == "exit")
            finish("pane " + paneId + " exited before the probe finished");
    }

    void handleInfo(const std::string& payload)
    {
        const auto panes = readListedPanes(payload);

        if (hasKilledPane)
            return confirmPaneIsGone(panes);

        std::printf("panes on %s:\n", arguments.destination.host.c_str());

        for (const auto& pane: panes)
            std::printf("  %s\n", pane.infoLine.c_str());

        if (arguments.mode == ProbeMode::AdoptPane)
            return adoptPane(findPaneToAdopt(panes));

        paneId = makeThrowawayPaneId();

        if (arguments.mode == ProbeMode::LeavePaneRunning)
            paneId = arguments.paneId;

        std::printf("opening throwaway pane %s\n", paneId.c_str());
        sendSpawn(probeSession, probeDirectory);
    }

    void confirmPaneIsGone(const std::vector<ListedPane>& panes)
    {
        for (const auto& pane: panes)
        {
            if (pane.id == paneId)
                return finish("the throwaway pane " + paneId + " is still listed after kill");
        }

        std::printf("throwaway pane %s is gone from the daemon's list\n", paneId.c_str());
        finish({});
    }

    std::optional<ListedPane> findPaneToAdopt(const std::vector<ListedPane>& panes) const
    {
        for (const auto& pane: panes)
        {
            if (arguments.paneId.empty() || pane.id == arguments.paneId)
                return pane;
        }

        return std::nullopt;
    }

    void adoptPane(const std::optional<ListedPane>& pane)
    {
        if (!pane)
            return finish("no such pane to adopt");

        paneId = pane->id;
        std::printf("adopting pane %s read only\n", paneId.c_str());
        sendSpawn(pane->session, {});
    }

    void sendSpawn(const std::string& session, const std::string& directory)
    {
        const auto columns = std::to_string(probeColumns);
        const auto rows = std::to_string(probeRows);
        link.send(term::proto::make("spawn", {paneId, columns, rows, session}, directory));
    }

    void handleOutput(const std::string& payload)
    {
        liveModel.feed(payload);
        const auto script = makePaneScript(arguments.mode);

        if (!hasSentCommand)
        {
            hasSentCommand = true;
            link.send(term::proto::make("write", {paneId}, script.command));
        }

        if (!hasSeenCommandRun && isShowingLine(liveModel.screen, script.lineThatShowsItRan))
        {
            hasSeenCommandRun = true;
            link.send(term::proto::make("release", {paneId}));
            handleCommandRan();
        }
    }

    void handleCommandRan()
    {
        if (arguments.mode == ProbeMode::LeavePaneRunning)
        {
            std::printf("left pane %s running on %s\n", paneId.c_str(), arguments.destination.host.c_str());
            return finish({});
        }

        sendSpawn(probeSession, probeDirectory);
    }

    void handleState(const std::string& payload)
    {
        const auto state = term::parseTerminalState(payload);

        if (!state)
            return finish("the daemon's state message did not parse");

        auto restored = term::TerminalModel {probeColumns, probeRows, theme};
        restored.restoreState(*state);
        std::printf("screen of %s, loaded from the daemon's state message:\n", paneId.c_str());
        printScreen(restored.screen);

        if (arguments.mode == ProbeMode::AdoptPane)
        {
            link.send(term::proto::make("release", {paneId}));
            return finish({});
        }

        killPane(paneId);
    }

    void killPane(const std::string& id)
    {
        paneId = id;
        link.send(term::proto::make("kill", {paneId}));
        hasKilledPane = true;
        std::printf("killed throwaway pane %s\n", paneId.c_str());
        link.send(term::proto::make("info"));
    }

    void finish(const std::string& reason)
    {
        if (isFinished)
            return;

        isFinished = true;
        failure = reason;
    }

    ProbeArguments arguments;
    term::EmbeddedSshLink link;
    term::Theme theme;
    term::TerminalModel liveModel {probeColumns, probeRows, theme};
    std::string paneId;
    bool hasSentCommand = false;
    bool hasSeenCommandRun = false;
    bool hasKilledPane = false;
    bool isFinished = false;
    std::string failure;
};

std::string readEnvironment(const char* name)
{
    const auto* value = std::getenv(name);
    return value != nullptr ? std::string {value} : std::string {};
}

// The agent when there is one, else the first of the usual key files.
term::SshCredentials readCredentials()
{
    const auto sshDirectory = readEnvironment("HOME") + "/.ssh/";
    auto credentials = term::SshCredentials {};
    credentials.agentSocketPath = readEnvironment("SSH_AUTH_SOCK");
    credentials.knownHostsPath = sshDirectory + "known_hosts";

    if (!credentials.agentSocketPath.empty())
        return credentials;

    for (const auto* keyName: {"id_ecdsa", "id_rsa", "id_ed25519"})
    {
        if (Files::exists(sshDirectory + keyName))
        {
            credentials.privateKeyPath = sshDirectory + keyName;
            return credentials;
        }
    }

    return credentials;
}
} // namespace

int main(int argc, char** argv)
{
    const auto arguments = parseProbeArguments({argv + 1, argv + argc});

    if (!arguments)
    {
        std::fprintf(stderr,
                     "usage: CowTermEmbeddedSshProbe <user@host[:port]> "
                     "[--adopt [pane id] | --leave-pane <pane id> | --kill-pane <pane id>]\n");
        return usageExitCode;
    }

    const auto credentials = readCredentials();
    return Apps::run([&] { Apps::quit(EmbeddedSshProbe {*arguments, credentials}.run()); });
}
