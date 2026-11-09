// term: the session daemon. Owns every PTY so shells survive the GUI —
// quit, crash or update the app and the processes keep running here. The
// app dials in over eacp IPC (one Messenger conversation), spawns or adopts
// shells by stable id, and streams input/output through this process.
// Each pane's output also runs through a TermScreen here, so an attaching
// app gets the whole terminal state, and that state is saved to disk so a
// pane comes back with its screen and history after the daemon restarts.

#include "Protocol.h"
#include "Signals.h"
#include "CowTermCore/Pty.h"
#include "CowTermCore/TerminalModel.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/Core.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Network/IPC/Messenger.h>
#include <emberstore/AppDatabase.h>

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <string_view>

using namespace eacp;

namespace
{
constexpr int idleExitSeconds = 60;
constexpr int saveIntervalMilliseconds = 5000;

// Default-coloured cells are re-coloured by the app's theme on restore; the
// daemon has no config, so it resolves everything else with the built-in one.
const term::Theme daemonTheme {};

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
};

// A named instance keeps its panes apart from the user's live ones.
std::string paneStoreName()
{
    const auto suffix = term::proto::instanceSuffix();
    return suffix.empty() ? std::string {"cowterm-panes"} : "cowterm-panes-" + suffix;
}

struct Daemon
{
    Daemon()
    {
        term::ignoreHangups();

        // The daemon is a headless PTY server with no UI. Its executable
        // lives inside CowTerm.app/Contents/MacOS, so [NSBundle mainBundle]
        // resolves to CowTerm.app and eacp's loop would otherwise give it the
        // app's regular activation policy — a second "CowTerm" tile in the
        // Dock and the app switcher. Drop out of the Dock entirely.
        Apps::setDockIconVisible(false);

        server.onClient = [this](IPC::Messenger& messenger)
        { adoptClient(messenger); };
    }

    void adoptClient(IPC::Messenger& messenger)
    {
        client = &messenger;

        messenger.onMessage = [this, &messenger](const std::string& body)
        {
            if (client == &messenger)
                handle(term::proto::parse(body));
        };

        messenger.onDisconnected = [this, &messenger]
        {
            if (client == &messenger)
                client = nullptr;
        };
    }

    void send(const std::string& message)
    {
        if (client != nullptr)
            client->send(message);
    }

    struct MessageHandler
    {
        std::string_view verb;
        std::size_t minimumArgumentCount = 0;
        void (Daemon::*handle)(const term::proto::Message& message);
    };

    void handle(const term::proto::Message& message)
    {
        static constexpr auto handlers = std::array {
            MessageHandler {"spawn", 3, &Daemon::handleSpawn},
            MessageHandler {"write", 1, &Daemon::handleWrite},
            MessageHandler {"resize", 3, &Daemon::handleResize},
            MessageHandler {"kill", 1, &Daemon::handleKill},
            MessageHandler {"info", 0, &Daemon::handleInfo},
            MessageHandler {"quit-server", 0, &Daemon::handleQuitServer},
        };

        const auto found = std::ranges::find(handlers, message.verb, &MessageHandler::verb);

        if (found == handlers.end() || message.args.size() < found->minimumArgumentCount)
            return;

        (this->*(found->handle))(message);
    }

    void handleSpawn(const term::proto::Message& message)
    {
        spawn(message.args[0],
              std::stoi(message.args[1]),
              std::stoi(message.args[2]),
              message.payload);
    }

    void handleWrite(const term::proto::Message& message)
    {
        write(message.args[0], message.payload);
    }

    void handleResize(const term::proto::Message& message)
    {
        resize(message.args[0], std::stoi(message.args[1]), std::stoi(message.args[2]));
    }

    void handleKill(const term::proto::Message& message)
    {
        kill(message.args[0]);
    }

    void handleInfo(const term::proto::Message&)
    {
        sendInfo();
    }

    void handleQuitServer(const term::proto::Message&)
    {
        Apps::quit();
    }

    ~Daemon() { saveAllPanes(); }

    void spawn(const std::string& id, int cols, int rows, const std::string& cwd)
    {
        if (auto found = panes.find(id); found != panes.end())
        {
            if (found->second->alive)
            {
                adopt(id, *found->second);
                return;
            }

            panes.erase(found);
        }

        auto& pane = *(panes[id] = std::make_unique<DaemonPane>(term::PtySize {cols, rows}));
        restoreSavedPane(id, pane);

        const auto started = pane.pty.start(
            {{cols, rows}, cwd, {}, {{term::proto::paneEnvironmentVariable, id}}},
            [this, id](std::string data)
            {
                Threads::callAsync([this, id, data = std::move(data)]
                                   { deliverOutput(id, data); });
            },
            [this, id] { Threads::callAsync([this, id] { shellExited(id); }); });

        pane.alive = started;

        if (!started)
        {
            send(term::proto::make("exit", {id}));
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
        send(term::proto::make(
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
        send(term::proto::make("output", {id}, data));
    }

    void shellExited(const std::string& id)
    {
        if (panes.erase(id) > 0)
        {
            forgetSavedPane(id);
            send(term::proto::make("exit", {id}));
        }
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

    void forgetSavedPane(const std::string& id)
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

    void sendInfo()
    {
        auto payload = std::string {};

        for (auto& [id, pane]: panes)
        {
            payload += id + "\t" + pane->pty.foregroundProcess() + "\t"
                       + pane->pty.currentWorkingDirectory() + "\n";
        }

        send(term::proto::make("info", {}, payload));
    }

    // A daemon with nothing to hold and nobody attached retires itself.
    void checkIdle()
    {
        if (client == nullptr && panes.empty())
        {
            if (++idleTicks * 10 >= idleExitSeconds)
                Apps::quit();
        }
        else
        {
            idleTicks = 0;
        }
    }

    std::map<std::string, std::unique_ptr<DaemonPane>> panes;
    IPC::Messenger* client = nullptr;
    int idleTicks = 0;
    emberstore::Database paneStore = emberstore::databaseForApp(
        "tamber", paneStoreName(), emberstore::Durability::Atomic);

    IPC::MessageServer server {term::proto::serverName()};
    Threads::Timer idleTimer {[this] { checkIdle(); }, 10};
    Threads::Timer saveTimer {[this] { saveChangedPanes(); }, Time::MS {saveIntervalMilliseconds}};
};
} // namespace

int main()
{
    try
    {
        return Apps::run<Daemon>();
    }
    catch (const std::exception&)
    {
        // Another daemon already holds the name; nothing to do.
        return 0;
    }
}
