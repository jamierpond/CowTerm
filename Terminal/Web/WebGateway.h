#pragma once

#include "../Pty.h"
#include "../Session.h"
#include "WebSocket.h"

#include <eacp/Network/Network.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace term::web
{
// The web UI's host: an eacp HTTP server (REST + the embedded client page)
// on config.webPort and a WebSocket listener one port up that streams pane
// output and accepts input. Main-thread object — REST handlers arrive
// through the EventLoop dispatcher and the socket threads marshal in via
// callAsync, so everything below the public surface shares the app's main
// thread with SessionManager. Full protocol: DESIGN-WEBUI.md.
class WebGateway
{
public:
    WebGateway(const AppConfig& configToUse, SessionManager& managerToUse);
    ~WebGateway();

    // AppShell forwards these from its SessionManager hooks. sessionsChanged
    // coalesces: any burst within one loop tick becomes a single push, so
    // callers can fire it on every title twitch without spamming clients.
    void wirePane(TerminalView& pane);
    void sessionsChanged();

    bool isRunning() const { return running; }

    // The actually-served HTTP port (0 while the gateway is off — disabled
    // by config, or its ports were taken by another instance).
    int port() const;

    // Whether this gateway is reachable beyond loopback.
    bool servesNetwork() const { return running && config.webBind == "any"; }

private:
    struct Client
    {
        WsConnection::Ptr connection;
        std::set<std::string> panes;
    };

    // A command running on this machine purely for a remote viewer (the
    // lazygit popup). It belongs to no session and never appears in the
    // roster or on this machine's screen — the viewer that asked for it
    // owns it, sizes it, and its exit tears it down.
    struct Ephemeral
    {
        std::unique_ptr<Pty> pty;
        int cols = 80;
        int rows = 24;
    };

    eacp::HTTP::Response route(const eacp::HTTP::Request& request);
    eacp::HTTP::Response paneRoute(const eacp::HTTP::Request& request,
                                   const std::string& id,
                                   const std::string& action);

    void adoptSocket(const WsConnection::Ptr& connection);
    void handleClientMessage(WsConnection* connection, const std::string& body);
    void dropClient(WsConnection* connection);
    void broadcastSessions();

    Client* clientFor(WsConnection* connection);
    TerminalView* findPane(const std::string& id) const;
    TermSession* sessionForPane(const std::string& id) const;
    void paneOutput(const std::string& id, std::string_view data);
    std::string sessionsJson() const;

    void runCommand(const wire::ClientOp& op);
    void startEphemeral(WsConnection* connection, const wire::ClientOp& op);
    void endEphemeral(const std::string& id);

    const AppConfig& config;
    SessionManager& manager;
    eacp::HTTP::Server http;
    std::unique_ptr<WsListener> socketListener;
    std::vector<Client> clients;
    std::map<std::string, Ephemeral> ephemerals;
    bool running = false;
    bool broadcastPending = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
} // namespace term::web
