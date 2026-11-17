// term: the session daemon. Owns every PTY so shells survive the GUI —
// quit, crash or update the app and the processes keep running here. The
// app dials in over eacp IPC (one Messenger conversation), spawns or adopts
// shells by stable id, and streams input/output through this process.
// Each pane's output also runs through a TermScreen here, so an attaching
// app gets the whole terminal state, and that state is saved to disk so a
// pane comes back with its screen and history after the daemon restarts.

#include "Multiplexer/DataDirectories.h"
#include "Multiplexer/PaneSizeOwner.h"
#include "Multiplexer/Protocol.h"
#include "Signals.h"
#include "CommandLookup.h"
#include "Shell/Pty.h"
#include "Emulator/TerminalModel.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/Core.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Network/IPC/Messenger.h>
#if EACP_CRASH_REPORTING
#include <PondVersion.h>
#include <eacp/CrashReporting/CrashReporting.h>
#endif
#include <emberstore/AppDatabase.h>
#include <SessionLog/EventLine.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace eacp;

namespace
{
constexpr auto idleCheckInterval = Time::MS {10000};
constexpr int idleChecksBeforeExit = 6;
constexpr int saveIntervalMilliseconds = 5000;

// Default-coloured cells are re-coloured by the app's theme on restore; the
// daemon has no config, so it resolves everything else with the built-in one.
const term::Theme daemonTheme {};

// What the overview shows for a pane: when it was last typed into and last
// printed, and what its program said it is doing (`ct pane-state`).
struct PaneActivity
{
    std::int64_t lastInputMilliseconds = 0;
    std::int64_t lastOutputMilliseconds = 0;
    std::string state;
    std::int64_t stateSinceMilliseconds = 0;
    std::string stateMessage;
};

struct DaemonPane
{
    explicit DaemonPane(const term::PtySize& sizeToUse)
        : size(sizeToUse)
        , terminal(size.cols, size.rows, daemonTheme)
    {
    }

    void restoreState(const term::TerminalState& state)
    {
        terminal.restoreState(state);
        terminal.screen.resize(size.cols, size.rows);
    }

    term::Pty pty;
    term::PtySize size;
    term::TerminalModel terminal;
    bool alive = false;
    bool hasUnsavedChanges = false;
    std::string sessionName;
    term::PaneSizeOwner sizeOwner;
    PaneActivity activity;

    // The clients showing this pane: its output, state and exit go to them.
    std::vector<IPC::Messenger*> watchers;
};

// What a client said about itself in "hello". One that said nothing speaks
// the protocol from before clients introduced themselves.
struct ConnectedClient
{
    term::PaneSizeOwner::ClientId id = 0;
    int protocolVersion = term::proto::sizeOwnerProtocolVersion - 1;
    std::string name;
};

struct LastInput
{
    std::string paneId;
    std::int64_t millisecondsSinceEpoch = 0;
};

std::int64_t getMillisecondsSinceEpoch()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// Moves the panes from where an older daemon kept them, before opening them.
emberstore::Database openPaneStore()
{
    if (auto move = term::moveOldPaneStore())
        LOG(sessionlog::formatEventLine(
            "daemon.migrate",
            {{"from", move->from.string()}, {"to", move->to.string()}, {"error", move->error}}));

    return emberstore::Database {eacp::FilePath {term::getPaneStoreDirectory().string()},
                                 emberstore::Durability::Atomic};
}

struct SpawnRequest
{
    std::string id;
    term::PtySize size;
    term::proto::SpawnPayload payload;
    std::string sessionName;
};

struct Daemon
{
    Daemon()
    {
        term::ignoreHangups();
        term::releaseInheritedStreams();

        // The daemon is a headless PTY server with no UI. Its executable
        // lives inside CowTerm.app/Contents/MacOS, so [NSBundle mainBundle]
        // resolves to CowTerm.app and eacp's loop would otherwise give it the
        // app's regular activation policy — a second "CowTerm" tile in the
        // Dock and the app switcher. Drop out of the Dock entirely.
        Apps::setDockIconVisible(false);

        server.onClient = [this](IPC::Messenger& messenger)
        { adoptClient(messenger); };
    }

    // Every Mac that attaches (the local app, each `ct bridge`) is a client
    // of its own; a pane's messages go only to the clients showing it.
    void adoptClient(IPC::Messenger& messenger)
    {
        auto client = ConnectedClient {};
        client.id = ++lastClientId;
        clients[&messenger] = client;

        messenger.onMessage = [this, &messenger](const std::string& body)
        { handle(messenger, term::proto::parse(body)); };

        messenger.onDisconnected = [this, &messenger] { forgetClient(messenger); };
    }

    void forgetClient(IPC::Messenger& messenger)
    {
        for (auto& [id, pane]: panes)
            stopWatching(id, *pane, messenger);

        clients.erase(&messenger);
    }

    void stopWatching(const std::string& id, DaemonPane& pane, IPC::Messenger& messenger)
    {
        std::erase(pane.watchers, &messenger);

        if (auto size = pane.sizeOwner.forgetClient(clients.at(&messenger).id))
            applySize(id, pane, *size);
    }

    void sendToWatchers(const DaemonPane& pane, const std::string& message)
    {
        for (auto* watcher: pane.watchers)
            watcher->send(message);
    }

    static void watch(DaemonPane& pane, IPC::Messenger& messenger)
    {
        if (std::ranges::find(pane.watchers, &messenger) == pane.watchers.end())
            pane.watchers.push_back(&messenger);
    }

    struct MessageHandler
    {
        std::string_view verb;
        std::size_t minimumArgumentCount = 0;
        void (Daemon::*handle)(IPC::Messenger& sender, const term::proto::Message& message);
    };

    void handle(IPC::Messenger& sender, const term::proto::Message& message)
    {
        static constexpr auto handlers = std::array {
            MessageHandler {"hello", 0, &Daemon::handleHello},
            MessageHandler {"spawn", 3, &Daemon::handleSpawn},
            MessageHandler {"release", 1, &Daemon::handleRelease},
            MessageHandler {"write", 1, &Daemon::handleWrite},
            MessageHandler {"resize", 3, &Daemon::handleResize},
            MessageHandler {"claim-size", 3, &Daemon::handleClaimSize},
            MessageHandler {"reclaim-sizes", 0, &Daemon::handleReclaimSizes},
            MessageHandler {"last-input", 0, &Daemon::handleLastInput},
            MessageHandler {"pane-state", 2, &Daemon::handlePaneState},
            MessageHandler {"activity", 0, &Daemon::handleActivity},
            MessageHandler {"kill", 1, &Daemon::handleKill},
            MessageHandler {"info", 0, &Daemon::handleInfo},
            MessageHandler {"layout", 1, &Daemon::handleLayout},
            MessageHandler {"layouts", 0, &Daemon::handleLayouts},
            MessageHandler {"quit-server", 0, &Daemon::handleQuitServer},
        };

        const auto found = std::ranges::find(handlers, message.verb, &MessageHandler::verb);

        if (found == handlers.end() || message.args.size() < found->minimumArgumentCount)
            return;

        (this->*(found->handle))(sender, message);
    }

    // hello [protocol version] [client name]
    void handleHello(IPC::Messenger& sender, const term::proto::Message& message)
    {
        auto& client = clients.at(&sender);

        if (!message.args.empty())
            client.protocolVersion = std::stoi(message.args[0]);

        if (message.args.size() > 1)
            client.name = term::proto::decodeArgument(message.args[1]);

        sender.send(term::proto::make(
            "daemon-hello", {std::to_string(term::proto::daemonProtocolVersion)}));
    }

    // spawn <id> <cols> <rows> [session], payload the working directory.
    void handleSpawn(IPC::Messenger& sender, const term::proto::Message& message)
    {
        constexpr auto sessionArgument = std::size_t {3};
        const auto sessionName =
            message.args.size() > sessionArgument ? message.args[sessionArgument] : std::string {};

        spawn(sender,
              {message.args[0],
               {std::stoi(message.args[1]), std::stoi(message.args[2])},
               term::proto::parseSpawnPayload(message.payload),
               sessionName});
    }

    void handleRelease(IPC::Messenger& sender, const term::proto::Message& message)
    {
        if (auto found = panes.find(message.args[0]); found != panes.end())
            stopWatching(found->first, *found->second, sender);
    }

    // write <id> [input], payload the bytes.
    void handleWrite(IPC::Messenger&, const term::proto::Message& message)
    {
        const auto& id = message.args[0];
        constexpr auto markerArgument = std::size_t {1};
        const auto isUserInput = message.args.size() > markerArgument
                                 && message.args[markerArgument] == term::proto::userInputMarker;

        if (auto found = panes.find(id); isUserInput && found != panes.end())
        {
            lastInput = {id, getMillisecondsSinceEpoch()};
            found->second->activity.lastInputMilliseconds = lastInput.millisecondsSinceEpoch;
        }

        write(id, message.payload);
    }

    static term::PtySize readSize(const term::proto::Message& message)
    {
        return {std::stoi(message.args[1]), std::stoi(message.args[2])};
    }

    // A client from before size owners resizes the way it always did: the
    // last one to resize sets the size.
    void handleResize(IPC::Messenger& sender, const term::proto::Message& message)
    {
        const auto& client = clients.at(&sender);

        if (client.protocolVersion < term::proto::sizeOwnerProtocolVersion)
            return claimSize(message.args[0], client, readSize(message));

        if (auto found = panes.find(message.args[0]); found != panes.end())
        {
            if (auto size = found->second->sizeOwner.requestSize(client.id, readSize(message)))
                applySize(found->first, *found->second, *size);
        }
    }

    // claim-size <id> <cols> <rows>: the sender sets this pane's size from now on.
    void handleClaimSize(IPC::Messenger& sender, const term::proto::Message& message)
    {
        claimSize(message.args[0], clients.at(&sender), readSize(message));
    }

    void claimSize(const std::string& id, const ConnectedClient& client, const term::PtySize& size)
    {
        if (auto found = panes.find(id); found != panes.end())
            applySize(id, *found->second, found->second->sizeOwner.claimSize(client.id, size));
    }

    // The sender takes back every pane it shows, at the sizes it last asked for.
    void handleReclaimSizes(IPC::Messenger& sender, const term::proto::Message&)
    {
        const auto& client = clients.at(&sender);

        for (auto& [id, pane]: panes)
        {
            if (std::ranges::find(pane->watchers, &sender) == pane->watchers.end())
                continue;

            if (auto size = pane->sizeOwner.reclaimSize(client.id))
                applySize(id, *pane, *size);
        }
    }

    // last-input [<id> <milliseconds since 1970>]: the pane the user last typed
    // into, through any client, and when; no arguments when nobody has.
    void handleLastInput(IPC::Messenger& sender, const term::proto::Message&)
    {
        if (lastInput.paneId.empty() || !panes.contains(lastInput.paneId))
            return sender.send(term::proto::make("last-input"));

        sender.send(term::proto::make(
            "last-input", {lastInput.paneId, std::to_string(lastInput.millisecondsSinceEpoch)}));
    }

    // pane-state <id> <working|waiting|finished>, payload what it says.
    void handlePaneState(IPC::Messenger&, const term::proto::Message& message)
    {
        if (auto found = panes.find(message.args[0]); found != panes.end())
        {
            auto& activity = found->second->activity;
            activity.state = message.args[1];
            activity.stateMessage = message.payload;
            activity.stateSinceMilliseconds = getMillisecondsSinceEpoch();
        }
    }

    // One "<id>\t<last input>\t<last output>\t<state>\t<state since>\t<message>"
    // line per pane; times in milliseconds since 1970, 0 for never.
    void handleActivity(IPC::Messenger& sender, const term::proto::Message&)
    {
        auto payload = std::string {};

        for (const auto& [id, pane]: panes)
        {
            const auto& activity = pane->activity;
            payload += id + "\t" + std::to_string(activity.lastInputMilliseconds) + "\t"
                       + std::to_string(activity.lastOutputMilliseconds) + "\t" + activity.state
                       + "\t" + std::to_string(activity.stateSinceMilliseconds) + "\t"
                       + flattenToOneLine(activity.stateMessage) + "\n";
        }

        sender.send(term::proto::make("activity", {}, payload));
    }

    static std::string flattenToOneLine(std::string text)
    {
        std::ranges::replace(text, '\n', ' ');
        std::ranges::replace(text, '\t', ' ');
        return text;
    }

    void applySize(const std::string& id, DaemonPane& pane, const term::PtySize& size)
    {
        resize(id, size.cols, size.rows);
        announceSizeOwner(id, pane);
    }

    // size-owner <id> <cols> <rows> <1 when it is the receiver> [owner's name],
    // to every watcher that knows the message.
    void announceSizeOwner(const std::string& id, const DaemonPane& pane)
    {
        const auto owner = pane.sizeOwner.getOwner();

        for (auto* watcher: pane.watchers)
        {
            const auto& client = clients.at(watcher);

            if (client.protocolVersion < term::proto::sizeOwnerProtocolVersion)
                continue;

            const auto isReceiverTheOwner = owner == client.id;
            watcher->send(term::proto::make("size-owner",
                                            {id,
                                             std::to_string(pane.size.cols),
                                             std::to_string(pane.size.rows),
                                             isReceiverTheOwner ? "1" : "0",
                                             term::proto::encodeArgument(findClientName(owner))}));
        }
    }

    std::string findClientName(std::optional<term::PaneSizeOwner::ClientId> clientId) const
    {
        for (const auto& [messenger, client]: clients)
        {
            if (client.id == clientId)
                return client.name;
        }

        return {};
    }

    void handleKill(IPC::Messenger&, const term::proto::Message& message)
    {
        kill(message.args[0]);
    }

    void handleInfo(IPC::Messenger& sender, const term::proto::Message&)
    {
        sendInfo(sender);
    }

    // layout <session>, payload the session's saved layout (opaque here):
    // what another Mac needs to attach to the same panes.
    void handleLayout(IPC::Messenger&, const term::proto::Message& message)
    {
        layouts[message.args[0]] = message.payload;
    }

    void handleLayouts(IPC::Messenger& sender, const term::proto::Message&)
    {
        forgetLayoutsWithoutPanes();
        auto payload = std::string {};

        for (const auto& [name, layout]: layouts)
            payload += term::proto::make("layout", {name}, layout) + '\0';

        sender.send(term::proto::make("layouts", {}, payload));
    }

    void forgetLayoutsWithoutPanes()
    {
        std::erase_if(layouts,
                      [this](const auto& entry)
                      {
                          return std::ranges::none_of(
                              panes,
                              [&](const auto& pane)
                              { return pane.second->sessionName == entry.first; });
                      });
    }

    void handleQuitServer(IPC::Messenger&, const term::proto::Message&)
    {
        saveAllPanes();
        Apps::quit();
    }

    ~Daemon() { saveAllPanes(); }

    void spawn(IPC::Messenger& sender, const SpawnRequest& request)
    {
        const auto& id = request.id;

        // A command that isn't installed here: the app says so in the pane,
        // instead of a pane that opens and closes in the same instant.
        const auto& command = request.payload.command;
        auto environment = std::vector<term::EnvironmentVariable> {
            {term::proto::paneEnvironmentVariable, id}};

        if (!command.empty())
        {
            const auto searchPath = term::findCommandSearchPath();

            if (!term::isCommandInstalled(command, searchPath))
            {
                sender.send(term::proto::make("missing-command", {id}, term::getProgramName(command)));
                return;
            }

            environment.push_back({"PATH", searchPath});
        }

        const auto& client = clients.at(&sender);

        if (auto found = panes.find(id); found != panes.end())
        {
            if (found->second->alive)
            {
                watch(*found->second, sender);
                adopt(id, *found->second);

                if (auto size = found->second->sizeOwner.requestSize(client.id, request.size))
                    applySize(id, *found->second, *size);

                return;
            }

            panes.erase(found);
        }

        auto& pane = *(panes[id] = std::make_unique<DaemonPane>(request.size));
        pane.sessionName = request.sessionName;
        pane.sizeOwner.claimSize(client.id, request.size);
        watch(pane, sender);
        restoreSavedPane(id, pane);

        const auto started = pane.pty.start(
            {request.size,
             request.payload.workingDirectory,
             request.payload.command,
             environment},
            [this, id](std::string data)
            {
                Threads::callAsync([this, id, data = std::move(data)]
                                   { deliverOutput(id, data); });
            },
            [this, id] { Threads::callAsync([this, id] { shellExited(id); }); });

        pane.alive = started;

        if (!started)
        {
            sendToWatchers(pane, term::proto::make("exit", {id}));
            panes.erase(id);
        }
    }

    // A pane whose shell died with an earlier daemon: show what it had on
    // screen, with the new shell's prompt below it.
    void restoreSavedPane(const std::string& id, DaemonPane& pane)
    {
        auto document = paneDocument(id);

        if (!eacp::Files::exists(document.filePath()))
            return;

        const auto saved = term::parseTerminalState(eacp::Files::readFile(document.filePath()));

        if (!saved)
            return;

        pane.restoreState(*saved);
        term::startFreshShellBelow(pane.terminal.screen, pane.terminal.parser);
        sendState(id, pane);
    }

    void sendState(const std::string& id, const DaemonPane& pane)
    {
        sendToWatchers(pane, term::proto::make(
            "state", {id}, term::serializeTerminalState(pane.terminal.captureState())));
    }

    // Reattach a shell that kept running while no GUI was around: send its
    // terminal state, then nudge the winsize so full-screen apps repaint.
    void adopt(const std::string& id, DaemonPane& pane)
    {
        sendState(id, pane);

        pane.pty.resize({pane.size.cols, pane.size.rows + 1});
        pane.pty.resize(pane.size);
    }

    void deliverOutput(const std::string& id, const std::string& data)
    {
        auto found = panes.find(id);

        if (found == panes.end())
            return;

        found->second->terminal.feed(data);
        found->second->hasUnsavedChanges = true;
        found->second->activity.lastOutputMilliseconds = getMillisecondsSinceEpoch();
        sendToWatchers(*found->second, term::proto::make("output", {id}, data));
    }

    void shellExited(const std::string& id)
    {
        auto found = panes.find(id);

        if (found == panes.end())
            return;

        const auto watchers = std::move(found->second->watchers);
        panes.erase(found);
        forgetSavedPane(id);

        for (auto* watcher: watchers)
            watcher->send(term::proto::make("exit", {id}));
    }

    void write(const std::string& id, const std::string& data)
    {
        if (auto found = panes.find(id); found != panes.end())
            found->second->pty.write(data);
    }

    void resize(const std::string& id, int cols, int rows)
    {
        if (auto found = panes.find(id); found != panes.end())
        {
            found->second->size = {cols, rows};
            found->second->terminal.screen.resize(cols, rows);
            found->second->hasUnsavedChanges = true;
            found->second->pty.resize({cols, rows});
        }
    }

    void kill(const std::string& id)
    {
        panes.erase(id);
        forgetSavedPane(id);
    }

    emberstore::Document<term::TerminalState> paneDocument(const std::string& id) const
    {
        return paneStore.document<term::TerminalState>("pane-" + id);
    }

    void forgetSavedPane(const std::string& id) const
    {
        eacp::Files::removeFile(paneDocument(id).filePath());
    }

    void saveChangedPanes()
    {
        for (auto& [id, pane]: panes)
        {
            if (pane->hasUnsavedChanges)
            {
                paneDocument(id).write(pane->terminal.captureState());
                pane->hasUnsavedChanges = false;
            }
        }
    }

    void saveAllPanes()
    {
        for (auto& [id, pane]: panes)
            pane->hasUnsavedChanges = true;

        saveChangedPanes();
    }

    void sendInfo(IPC::Messenger& requester)
    {
        auto payload = std::string {};

        for (auto& [id, pane]: panes)
        {
            payload += id + "\t" + pane->pty.foregroundProcess() + "\t"
                       + pane->pty.currentWorkingDirectory() + "\t" + pane->sessionName
                       + "\n";
        }

        requester.send(term::proto::make("info", {}, payload));
    }

    // A daemon with nothing to hold and nobody attached retires itself.
    void checkIdle()
    {
        if (clients.empty() && panes.empty())
        {
            if (++idleTicks >= idleChecksBeforeExit)
                Apps::quit();
        }
        else
        {
            idleTicks = 0;
        }
    }

    std::map<std::string, std::unique_ptr<DaemonPane>> panes;
    std::map<IPC::Messenger*, ConnectedClient> clients;
    term::PaneSizeOwner::ClientId lastClientId = 0;
    LastInput lastInput;
    std::map<std::string, std::string> layouts;
    int idleTicks = 0;
    emberstore::Database paneStore = openPaneStore();

    IPC::MessageServer server {term::proto::serverName()};
    Threads::Timer idleTimer {[this] { checkIdle(); }, idleCheckInterval};
    Threads::Timer saveTimer {[this] { saveChangedPanes(); }, Time::MS {saveIntervalMilliseconds}};
};
} // namespace

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
#if EACP_CRASH_REPORTING
    if (auto handlerExitCode = CrashReporting::runHandlerIfRequested(argc, argv))
        return *handlerExitCode;

    auto crashReportingOptions = CrashReporting::Options {};
    crashReportingOptions.release = pond::getVersionString();
    CrashReporting::start(crashReportingOptions);
#endif

    try
    {
        return Apps::run<Daemon>();
    }
    catch (const std::exception& error)
    {
        // Usually another daemon already holds the name; nothing to do.
        std::fprintf(stderr, "cowtermd: %s\n", error.what());
        return 0;
    }
}
