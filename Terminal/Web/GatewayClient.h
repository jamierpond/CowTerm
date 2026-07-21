#pragma once

#include "../SessionCommand.h"
#include "../Shell.h"
#include "WebSocket.h"
#include "Wire.h"

#include <eacp/Core/Threads/Timer.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace term::web
{
// The native end of another CowTerm's gateway: one websocket carrying that
// machine's whole session roster plus every pane this instance is attached
// to — the same conversation the browser client holds, so the two piloting
// surfaces can never diverge. Dials in the background, redials on loss and
// re-attaches its panes, each reattach opening with a fresh snapshot.
//
// Main-thread object. Pane output is the one exception: it fires straight
// from the socket thread, because Shell's contract already says output may
// arrive on any thread and receivers marshal for themselves.
class GatewayClient
{
public:
    struct PaneRoutes
    {
        // Any thread.
        std::function<void(std::string)> onOutput = [](std::string) {};

        // Main thread. onExit means the remote pane is gone for good
        // (remote shell exited) — connection loss redials instead.
        eacp::Callback onExit = [] {};
        std::function<void(int cols, int rows)> onSize = [](int, int) {};
    };

    // "host" or "host:port"; a bare host gets the default gateway port.
    explicit GatewayClient(const std::string& addressToUse);
    ~GatewayClient();

    const std::string& address() const { return displayAddress; }
    const std::string& hostName() const { return host; }
    bool isConnected() const { return connected; }

    // This remote turned out to be the very instance holding it, so the
    // link was refused and will not be retried. Permanent for the run: it
    // takes a config edit (and a restart) to become a real peer.
    bool isSelf() const { return selfLink; }

    // The remote's roster as of the last sessions push.
    const std::vector<wire::SessionInfo>& sessions() const { return model; }
    const wire::PaneInfo* findPane(const std::string& paneId) const;

    void attach(const std::string& paneId, PaneRoutes routes);
    void detach(const std::string& paneId);
    void input(const std::string& paneId, std::string_view data);
    void activate(const std::string& sessionKey);

    // whenOpened fires with the new session's key once the remote has
    // created it, so the caller can mirror what it just asked for.
    void open(const std::string& dir,
              std::function<void(const std::string&)> whenOpened = {});

    // Leader-table actions that change the pane tree. They act on the
    // session owning paneId, which the remote focuses first.
    void command(const std::string& paneId,
                 SessionCommand action,
                 float cells = 1.0f);

    // Runs commandLine on the remote in paneId's directory, in a pane that
    // exists only for us; whenReady names it so we can attach and display
    // it here. One request at a time — the popup UI is single-instance.
    void popup(const std::string& paneId,
               const std::string& commandLine,
               int cols,
               int rows,
               std::function<void(const std::string&)> whenReady);

    // Ephemeral panes are ours to size, unlike session panes.
    void resize(const std::string& paneId, int cols, int rows);

    // Roster or connection state moved (main thread).
    eacp::Callback onChanged = [] {};

private:
    void dial();
    void adopt(const WsConnection::Ptr& socket);
    void handleEvent(const std::string& body);
    void handleBinary(const std::string& frame);
    void sendOp(const wire::ClientOp& op);

    std::string host;
    std::uint16_t port = 2697;
    std::string displayAddress;

    WsConnection::Ptr connection;
    std::atomic<bool> connected {false};
    std::atomic<bool> dialing {false};
    std::atomic<bool> selfLink {false};

    // Read on the socket thread for output routing; everything else main.
    std::mutex routesLock;
    std::map<std::string, PaneRoutes> routes;

    std::vector<wire::SessionInfo> model;
    std::function<void(const std::string&)> pendingPopup;
    std::function<void(const std::string&)> pendingOpen;
    std::unique_ptr<eacp::Threads::Timer> redial;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

// Owns the app's gateway links, one per configured remote, for the whole
// app lifetime — remote sessions hold RemoteShells pointing into these
// clients, so the fleet must outlive the SessionManager (it's declared
// above it in AppShell). Fans every client's change signal into one hook.
class RemoteFleet
{
public:
    explicit RemoteFleet(const std::vector<std::string>& addresses)
    {
        for (const auto& address: addresses)
        {
            auto& client =
                *clients.emplace_back(std::make_unique<GatewayClient>(address));
            client.onChanged = [this, raw = &client] { onChanged(*raw); };
        }
    }

    const std::vector<std::unique_ptr<GatewayClient>>& all() const
    {
        return clients;
    }

    // Roster or connection state moved on that client (main thread).
    std::function<void(GatewayClient&)> onChanged = [](GatewayClient&) {};

private:
    std::vector<std::unique_ptr<GatewayClient>> clients;
};

// A pane whose process lives inside another CowTerm, reached through that
// machine's gateway. terminate() and detach() both just let go — closing a
// window here must never kill work running over there. The grid mirrors
// the remote GUI's dimensions via fixedSize().
class RemoteShell final : public Shell
{
public:
    // ownsSize inverts who decides the grid: false for a session pane
    // (the remote GUI displays it too, so it dictates), true for an
    // ephemeral popup pane nobody else is watching.
    RemoteShell(GatewayClient& clientToUse,
                std::string paneIdToUse,
                bool ownsSizeToUse = false);

    bool start(const PtyOptions& options,
               std::function<void(std::string)> onOutput,
               std::function<void()> onExit) override;

    void write(std::string_view data) override;
    void resize(const PtySize& size) override;
    std::optional<PtySize> fixedSize() const override;

    std::string foregroundProcess() const override;
    std::string currentWorkingDirectory() const override;

    void terminate() override;
    void detach() override;

private:
    GatewayClient& client;
    std::string paneId;
    bool ownsSize = false;
    PtySize remoteSize {0, 0};
};
} // namespace term::web
