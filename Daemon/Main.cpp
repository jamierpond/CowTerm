// cowtermd: the session daemon. Owns every PTY so shells survive the GUI —
// quit, crash or update the app and the processes keep running here. The
// app dials in over eacp IPC (one Messenger conversation), spawns or adopts
// shells by stable id, and streams input/output through this process.

#include "Protocol.h"
#include "Pty.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/Core.h>
#include <eacp/Network/IPC/Messenger.h>

#include <algorithm>
#include <csignal>
#include <map>

#if !defined(_WIN32)
#include <unistd.h>
#endif
#include <memory>
#include <string>

using namespace eacp;

namespace
{
constexpr std::size_t replayBufferCap = 256 * 1024;
constexpr int idleExitSeconds = 60;

struct DaemonPane
{
    term::Pty pty;
    std::string replay;
    term::PtySize size;
    std::string startDir;
    bool alive = false;
};

struct Daemon
{
    Daemon()
    {
        // The GUI dying must never take the daemon with it. Windows has no
        // hangup signal; a detached process simply keeps running.
#if !defined(_WIN32)
        signal(SIGHUP, SIG_IGN);
        signal(SIGPIPE, SIG_IGN);
#endif

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

    void handle(const term::proto::Message& message)
    {
        if (message.verb == "spawn" && message.args.size() >= 3)
            spawn(message.args[0],
                  std::stoi(message.args[1]),
                  std::stoi(message.args[2]),
                  message.payload);
        else if (message.verb == "write" && !message.args.empty())
            write(message.args[0], message.payload);
        else if (message.verb == "resize" && message.args.size() >= 3)
            resize(message.args[0],
                   std::stoi(message.args[1]),
                   std::stoi(message.args[2]));
        else if (message.verb == "kill" && !message.args.empty())
            kill(message.args[0]);
        else if (message.verb == "info")
            sendInfo();
        else if (message.verb == "quit-server")
            Apps::quit();
    }

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

        auto& pane = *(panes[id] = std::make_unique<DaemonPane>());
        pane.size = {cols, rows};
        pane.startDir = cwd;

        const auto started = pane.pty.start(
            {{cols, rows}, cwd, {}},
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

    // Reattach a shell that kept running while no GUI was around: replay
    // what it printed, then nudge the winsize so full-screen apps repaint.
    void adopt(const std::string& id, DaemonPane& pane)
    {
        send(term::proto::make("output", {id}, pane.replay));

        pane.pty.resize({pane.size.cols, pane.size.rows + 1});
        pane.pty.resize(pane.size);
    }

    void deliverOutput(const std::string& id, const std::string& data)
    {
        auto found = panes.find(id);

        if (found == panes.end())
            return;

        auto& replay = found->second->replay;
        replay += data;

        if (replay.size() > replayBufferCap)
            replay.erase(0, replay.size() - replayBufferCap);

        send(term::proto::make("output", {id}, data));
    }

    void shellExited(const std::string& id)
    {
        if (panes.erase(id) > 0)
            send(term::proto::make("exit", {id}));
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
            found->second->pty.resize({cols, rows});
        }
    }

    void kill(const std::string& id) { panes.erase(id); }

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

    IPC::MessageServer server {term::proto::serverName()};
    Threads::Timer idleTimer {[this] { checkIdle(); }, 10};
};
} // namespace

int main()
{
    // The GUI spawns us, so we start holding copies of whatever it had
    // open — including the web gateway's listening sockets when a crashed
    // daemon is relaunched by an app that is already serving. We outlive
    // the GUI by design, so an inherited listener would keep the port
    // bound long after the app that owned it exited, and the next launch
    // would fail to bind with nothing to show for it. Nothing is handed to
    // us by descriptor; the IPC channel is opened below, by name.
#if !defined(_WIN32)
    constexpr auto highestPlausibleFd = 4096;
    const auto limit = std::min((int) ::sysconf(_SC_OPEN_MAX), highestPlausibleFd);

    for (auto descriptor = 3; descriptor < limit; ++descriptor)
        ::close(descriptor);
#endif

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
