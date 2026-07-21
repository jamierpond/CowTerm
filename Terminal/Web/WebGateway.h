#pragma once

#include "../Session.h"
#include "WebSocket.h"

#include <eacp/Network/Network.h>

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

    // AppShell forwards these from its SessionManager hooks.
    void wirePane(TerminalView& pane);
    void sessionsChanged();

    bool isRunning() const { return running; }

private:
    struct Client
    {
        WsConnection::Ptr connection;
        std::set<std::string> panes;
    };

    eacp::HTTP::Response route(const eacp::HTTP::Request& request);
    eacp::HTTP::Response paneRoute(const eacp::HTTP::Request& request,
                                   const std::string& id,
                                   const std::string& action);

    void adoptSocket(const WsConnection::Ptr& connection);
    void handleClientMessage(WsConnection* connection, const std::string& body);
    void dropClient(WsConnection* connection);

    Client* clientFor(WsConnection* connection);
    TerminalView* findPane(const std::string& id) const;
    void paneOutput(const std::string& id, std::string_view data);
    std::string sessionsJson() const;

    const AppConfig& config;
    SessionManager& manager;
    eacp::HTTP::Server http;
    std::unique_ptr<WsListener> socketListener;
    std::vector<Client> clients;
    bool running = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
} // namespace term::web
