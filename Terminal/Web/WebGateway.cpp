#include "WebGateway.h"
#include "ScreenSerializer.h"
#include "Wire.h"

#include <ResEmbed/ResEmbed.h>
#include <eacp/Core/Core.h>
#include <eacp/Network/HTTPServer/Json.h>
#include <eacp/Network/HTTPServer/ResponseBuilders.h>

#include <Miro/Reflect.h>

namespace term::web
{
using namespace eacp;
using namespace wire;

namespace
{
struct OpenRequest
{
    std::string dir;

    MIRO_REFLECT(dir)
};

struct ActivateRequest
{
    std::string key;

    MIRO_REFLECT(key)
};

struct TextRequest
{
    std::string text;

    MIRO_REFLECT(text)
};

struct OkResponse
{
    bool ok = true;

    MIRO_REFLECT(ok)
};

bool isLoopbackHost(const std::string& value)
{
    for (const auto* allowed: {"127.0.0.1", "localhost", "[::1]"})
        if (value.rfind(allowed, 0) == 0)
            return true;

    return false;
}

template <typename T>
HTTP::Response jsonResponse(const T& value, int status = 200)
{
    auto response = HTTP::Response {};
    response.statusCode = status;
    HTTP::Json::setJson(response, value);
    return response;
}
} // namespace

WebGateway::WebGateway(const AppConfig& configToUse, SessionManager& managerToUse)
    : config(configToUse)
    , manager(managerToUse)
    , http(HTTP::ServerOptions {
          .threading = HTTP::ServerThreadingMode::EventLoop,
          .bindTo = configToUse.webBind == "any" ? BindInterface::any
                                                 : BindInterface::loopback})
{
    if (config.webPort <= 0)
        return;

    try
    {
        socketListener = std::make_unique<WsListener>(
            config.webPort + 1,
            config.webBind == "any" ? BindInterface::any
                                    : BindInterface::loopback);
    }
    catch (const TCP::Error&)
    {
        // Port taken — likely another CowTerm already serves the web UI.
        return;
    }

    auto guard = std::weak_ptr<bool> {alive};

    socketListener->onConnection = [this, guard](WsConnection::Ptr connection)
    {
        Threads::callAsync(
            [this, guard, connection = std::move(connection)]
            {
                if (!guard.expired())
                    adoptSocket(connection);
            });
    };

    if (!http.listen(config.webPort,
                     [this](const HTTP::Request& request)
                     { return route(request); }))
    {
        socketListener.reset();
        return;
    }

    running = true;
}

WebGateway::~WebGateway()
{
    *alive = false;

    for (auto& client: clients)
        client.connection->close();

    socketListener.reset();
    http.stop();
}

int WebGateway::port() const
{
    return running ? http.boundPort() : 0;
}

void WebGateway::wirePane(TerminalView& pane)
{
    pane.onOutput = [this, &pane](std::string_view data)
    { paneOutput(pane.shellId(), data); };
}

void WebGateway::sessionsChanged()
{
    if (!running || broadcastPending || clients.empty())
        return;

    broadcastPending = true;

    Threads::callAsync(
        [this, guard = std::weak_ptr<bool> {alive}]
        {
            if (guard.expired())
                return;

            broadcastPending = false;
            broadcastSessions();
        });
}

void WebGateway::broadcastSessions()
{
    if (clients.empty())
        return;

    // Panes may have gone with their session: tell subscribers, then prune.
    auto live = std::set<std::string> {};

    for (const auto& session: manager.all())
        for (auto* pane: session->view.panes())
            live.insert(pane->shellId());

    const auto payload = sessionsJson();

    for (auto& client: clients)
    {
        for (auto it = client.panes.begin(); it != client.panes.end();)
        {
            if (live.count(*it) == 0)
            {
                client.connection->sendText(
                    Miro::toJSONString(ExitEvent {.pane = *it}));
                it = client.panes.erase(it);
            }
            else
            {
                ++it;
            }
        }

        client.connection->sendText(payload);
    }
}

HTTP::Response WebGateway::route(const HTTP::Request& request)
{
    // The one funnel every REST request passes through: the loopback Host
    // check (anti DNS-rebinding) lives here, and bearer-token auth slots in
    // beside it later. Bound to the network, peers legitimately dial by
    // machine name, so the check only applies to loopback binds.
    if (config.webBind != "any" && !isLoopbackHost(request.getHeader("Host")))
        return HTTP::makePlainTextResponse(403, "loopback only");

    const auto path = request.pathWithoutQuery();

    if (request.type == "GET" && (path == "/" || path == "/index.html"))
    {
        const auto page = ResEmbed::get("webui.html");

        if (page.size() == 0)
            return HTTP::makePlainTextResponse(500, "web UI asset missing");

        auto response = HTTP::Response {};
        response.statusCode = 200;
        response.setContent(
            std::string {(const char*) page.data(), page.size()},
            "text/html; charset=utf-8");
        return response;
    }

    if (request.type == "GET" && path == "/api/v1/server")
    {
        // The WS host must be whatever name the client dialed us by — a
        // LAN peer reaching "studio.local:2697" needs the socket there too.
        auto host = request.getHeader("Host");

        if (const auto colon = host.rfind(':'); colon != std::string::npos)
            host = host.substr(0, colon);

        if (host.empty())
            host = "127.0.0.1";

        auto info = ServerInfo {};
        info.wsUrl =
            "ws://" + host + ":" + std::to_string(socketListener->port()) + "/";
        return jsonResponse(info);
    }

    if (request.type == "GET" && path == "/api/v1/sessions")
    {
        auto response = HTTP::Response {};
        response.statusCode = 200;
        response.setContent(sessionsJson(), "application/json");
        return response;
    }

    if (request.type == "POST" && path == "/api/v1/sessions")
    {
        auto body = OpenRequest {};
        Miro::fromJSONString(body, request.body);

        if (body.dir.empty())
            return HTTP::makePlainTextResponse(400, "dir required");

        manager.openProject(expandHome(body.dir));
        return jsonResponse(OkResponse {});
    }

    if (request.type == "POST" && path == "/api/v1/sessions/activate")
    {
        auto body = ActivateRequest {};
        Miro::fromJSONString(body, request.body);

        if (auto* session = manager.find(body.key))
        {
            manager.switchTo(*session);
            return jsonResponse(OkResponse {});
        }

        return HTTP::makePlainTextResponse(404, "no such session");
    }

    // /api/v1/panes/{id}/{action}
    const auto prefix = std::string {"/api/v1/panes/"};

    if (path.rfind(prefix, 0) == 0)
    {
        const auto rest = path.substr(prefix.size());
        const auto slash = rest.find('/');

        if (slash != std::string::npos)
            return paneRoute(request,
                             rest.substr(0, slash),
                             rest.substr(slash + 1));
    }

    return HTTP::makePlainTextResponse(404, "Not Found");
}

HTTP::Response WebGateway::paneRoute(const HTTP::Request& request,
                                     const std::string& id,
                                     const std::string& action)
{
    auto* pane = findPane(id);

    if (pane == nullptr)
        return HTTP::makePlainTextResponse(404, "no such pane");

    if (request.type == "GET" && action == "screen")
    {
        auto response = HTTP::Response {};
        response.statusCode = 200;
        response.setContent(serializeScreen(pane->screenModel()),
                            "text/plain; charset=utf-8");
        return response;
    }

    if (request.type == "POST" && action == "input")
    {
        pane->writeToShell(request.body);
        return jsonResponse(OkResponse {});
    }

    if (request.type == "POST" && action == "text")
    {
        auto body = TextRequest {};
        Miro::fromJSONString(body, request.body);
        pane->writeToShell(body.text);
        return jsonResponse(OkResponse {});
    }

    return HTTP::makePlainTextResponse(404, "Not Found");
}

void WebGateway::adoptSocket(const WsConnection::Ptr& connection)
{
    auto guard = std::weak_ptr<bool> {alive};
    auto* raw = connection.get();

    connection->onText = [this, guard, raw](const std::string& body)
    {
        Threads::callAsync(
            [this, guard, raw, body]
            {
                if (!guard.expired())
                    handleClientMessage(raw, body);
            });
    };

    connection->onClosed = [this, guard, raw]
    {
        Threads::callAsync(
            [this, guard, raw]
            {
                if (!guard.expired())
                    dropClient(raw);
            });
    };

    clients.push_back({connection, {}});
    connection->start();
    connection->sendText(sessionsJson());
}

void WebGateway::handleClientMessage(WsConnection* connection,
                                     const std::string& body)
{
    auto* client = clientFor(connection);

    if (client == nullptr)
        return;

    auto op = ClientOp {};

    try
    {
        Miro::fromJSONString(op, body);
    }
    catch (const std::exception&)
    {
        connection->sendText(
            Miro::toJSONString(ErrorEvent {.message = "bad message"}));
        return;
    }

    const auto paneError = [&]
    {
        connection->sendText(Miro::toJSONString(
            ErrorEvent {.message = "no such pane: " + op.pane}));
    };

    if (op.op == "attach")
    {
        auto* pane = findPane(op.pane);

        if (pane == nullptr)
        {
            paneError();
            return;
        }

        client->panes.insert(op.pane);

        const auto& screen = pane->screenModel();
        connection->sendText(
            Miro::toJSONString(AttachedEvent {.pane = op.pane,
                                              .cols = screen.columns(),
                                              .rows = screen.rows()}));
        connection->sendBinary(binaryFrame(op.pane, serializeScreen(screen)));
    }
    else if (op.op == "detach")
    {
        client->panes.erase(op.pane);
    }
    else if (op.op == "input")
    {
        if (auto* pane = findPane(op.pane))
            pane->writeToShell(op.data);
        else
            paneError();
    }
    else if (op.op == "sessions")
    {
        connection->sendText(sessionsJson());
    }
    else if (op.op == "activate")
    {
        if (auto* session = manager.find(op.key))
            manager.switchTo(*session);
    }
    else if (op.op == "open")
    {
        if (!op.dir.empty())
            manager.openProject(expandHome(op.dir));
    }
    else
    {
        connection->sendText(Miro::toJSONString(
            ErrorEvent {.message = "unknown op: " + op.op}));
    }
}

void WebGateway::dropClient(WsConnection* connection)
{
    std::erase_if(clients,
                  [connection](const Client& client)
                  { return client.connection.get() == connection; });
}

WebGateway::Client* WebGateway::clientFor(WsConnection* connection)
{
    for (auto& client: clients)
        if (client.connection.get() == connection)
            return &client;

    return nullptr;
}

TerminalView* WebGateway::findPane(const std::string& id) const
{
    for (const auto& session: manager.all())
        for (auto* pane: session->view.panes())
            if (pane->shellId() == id)
                return pane;

    return nullptr;
}

void WebGateway::paneOutput(const std::string& id, std::string_view data)
{
    if (clients.empty())
        return;

    auto frame = std::string {};

    for (auto& client: clients)
    {
        if (client.panes.count(id) == 0)
            continue;

        if (frame.empty())
            frame = binaryFrame(id, data);

        client.connection->sendBinary(frame);
    }
}

std::string WebGateway::sessionsJson() const
{
    auto event = SessionsEvent {};

    for (const auto& session: manager.all())
    {
        auto info = SessionInfo {};
        info.key = session->key();
        info.name = session->name;
        info.projectDir = session->projectDir;
        info.active = manager.active() == session.get();
        info.claude = session->isClaude();

        for (auto* pane: session->view.panes())
        {
            auto paneInfo = PaneInfo {};
            paneInfo.id = pane->shellId();
            paneInfo.title = pane->currentTitle();
            paneInfo.cwd = pane->workingDirectory();
            paneInfo.cols = pane->screenModel().columns();
            paneInfo.rows = pane->screenModel().rows();
            paneInfo.active = session->view.activePane() == pane;
            info.panes.push_back(std::move(paneInfo));
        }

        event.sessions.push_back(std::move(info));
    }

    return Miro::toJSONString(event);
}
} // namespace term::web
