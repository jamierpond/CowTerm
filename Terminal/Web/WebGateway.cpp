#include "WebGateway.h"
#include "../SessionCommand.h"
#include "ScreenSerializer.h"
#include "Wire.h"

#include <cstdio>
#include <random>

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

// A browser stamps Origin on cross-site requests; a "simple" POST needs no
// CORS preflight, so a malicious page could fire input at this gateway and
// the Host header would still look right. The Origin is the tell: writes
// are only accepted from our own pages (or non-browser clients, which send
// no Origin at all).
bool originAllowsWrites(const eacp::HTTP::Request& request)
{
    const auto origin = request.getHeader("Origin");

    if (origin.empty() || request.type == "GET")
        return true;

    if (origin == "http://" + request.getHeader("Host"))
        return true;

    for (const auto* allowed:
         {"http://127.0.0.1", "http://localhost", "http://[::1]"})
        if (origin.rfind(allowed, 0) == 0)
            return true;

    return false;
}

std::string generateEphemeralId()
{
    auto device = std::random_device {};
    auto engine = std::mt19937_64 {device()};
    char buffer[32];
    std::snprintf(
        buffer, sizeof(buffer), "popup-%016llx", (unsigned long long) engine());
    return buffer;
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

    pane.onGridResized = [this] { sessionsChanged(); };
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

    if (!originAllowsWrites(request))
        return HTTP::makePlainTextResponse(403, "cross-origin writes refused");

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
        info.instanceId = localInstanceId();
        info.remotes = config.remotes;

        // Discovery (and only discovery) is cross-origin readable: the web
        // UI served by one CowTerm fans out to its peers' gateways, and the
        // browser needs this one GET from a foreign origin. Everything
        // else stays same-origin; the live surface is the websocket, whose
        // handshake does its own origin policing.
        auto response = jsonResponse(info);
        response.setHeader("Access-Control-Allow-Origin", "*");
        return response;
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
        // An ephemeral pane is born attached and empty — no snapshot, it
        // has printed nothing yet.
        if (auto found = ephemerals.find(op.pane); found != ephemerals.end())
        {
            client->panes.insert(op.pane);
            connection->sendText(
                Miro::toJSONString(AttachedEvent {.pane = op.pane,
                                                  .cols = found->second.cols,
                                                  .rows = found->second.rows}));
            return;
        }

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

        // Nobody left watching a popup means the command has no audience;
        // it exists only for its viewer.
        if (ephemerals.count(op.pane) > 0)
            endEphemeral(op.pane);
    }
    else if (op.op == "input")
    {
        if (auto found = ephemerals.find(op.pane); found != ephemerals.end())
            found->second.pty->write(op.data);
        else if (auto* pane = findPane(op.pane))
            pane->writeToShell(op.data);
        else
            paneError();
    }
    else if (op.op == "command")
    {
        runCommand(op);
    }
    else if (op.op == "popup")
    {
        startEphemeral(connection, op);
    }
    else if (op.op == "resize")
    {
        // Only ephemeral panes take their size from a viewer; a session
        // pane's grid belongs to the GUI that owns it.
        if (auto found = ephemerals.find(op.pane);
            found != ephemerals.end() && op.cols > 0 && op.rows > 0)
        {
            found->second.cols = op.cols;
            found->second.rows = op.rows;
            found->second.pty->resize({op.cols, op.rows});
        }
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
        {
            auto& opened = manager.openProject(expandHome(op.dir));
            connection->sendText(
                Miro::toJSONString(OpenedEvent {.key = opened.key()}));
        }
    }
    else
    {
        connection->sendText(Miro::toJSONString(
            ErrorEvent {.message = "unknown op: " + op.op}));
    }
}

void WebGateway::dropClient(WsConnection* connection)
{
    // A departing viewer takes its popups with it — they run on this
    // machine but exist only for that viewer.
    if (auto* client = clientFor(connection))
    {
        auto owned = std::vector<std::string> {};

        for (const auto& paneId: client->panes)
            if (ephemerals.count(paneId) > 0)
                owned.push_back(paneId);

        for (const auto& paneId: owned)
            endEphemeral(paneId);
    }

    std::erase_if(clients,
                  [connection](const Client& client)
                  { return client.connection.get() == connection; });
}

void WebGateway::runCommand(const wire::ClientOp& op)
{
    const auto command = commandFromName(op.command);

    if (command == SessionCommand::None)
        return;

    auto* session = sessionForPane(op.pane);
    auto* pane = findPane(op.pane);

    if (session == nullptr || pane == nullptr)
        return;

    // Commands act on the pane the caller is in, so adopt it as active
    // first — the same thing clicking into a pane does locally.
    session->view.focusPane(pane);
    applySessionCommand(session->view, command, op.cells > 0 ? op.cells : 1.0f);

    sessionsChanged();
}

void WebGateway::startEphemeral(WsConnection* connection, const wire::ClientOp& op)
{
    if (op.data.empty())
        return;

    auto* pane = findPane(op.pane);

    if (pane == nullptr)
        return;

    auto* client = clientFor(connection);

    if (client == nullptr)
        return;

    const auto id = generateEphemeralId();
    auto entry = Ephemeral {};
    entry.pty = std::make_unique<Pty>();
    entry.cols = op.cols > 0 ? op.cols : 80;
    entry.rows = op.rows > 0 ? op.rows : 24;

    auto guard = std::weak_ptr<bool> {alive};

    const auto started = entry.pty->start(
        {{entry.cols, entry.rows}, pane->workingDirectory(), op.data},
        [this, guard, id](std::string data)
        {
            Threads::callAsync(
                [this, guard, id, data = std::move(data)]
                {
                    if (!guard.expired())
                        paneOutput(id, data);
                });
        },
        [this, guard, id]
        {
            Threads::callAsync(
                [this, guard, id]
                {
                    if (!guard.expired())
                        endEphemeral(id);
                });
        });

    if (!started)
        return;

    ephemerals[id] = std::move(entry);
    client->panes.insert(id);

    connection->sendText(Miro::toJSONString(PopupEvent {.pane = id}));
    connection->sendText(Miro::toJSONString(
        AttachedEvent {.pane = id,
                       .cols = ephemerals[id].cols,
                       .rows = ephemerals[id].rows}));
}

void WebGateway::endEphemeral(const std::string& id)
{
    auto found = ephemerals.find(id);

    if (found == ephemerals.end())
        return;

    // Move the PTY out before announcing: the exit callback that brought
    // us here belongs to it, and it must not die mid-invocation.
    auto dying = std::move(found->second);
    ephemerals.erase(found);

    for (auto& client: clients)
        if (client.panes.erase(id) > 0)
            client.connection->sendText(
                Miro::toJSONString(ExitEvent {.pane = id}));

    Threads::callAsync([held = std::shared_ptr<Pty>(std::move(dying.pty))] {});
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

TermSession* WebGateway::sessionForPane(const std::string& id) const
{
    for (const auto& session: manager.all())
        for (auto* pane: session->view.panes())
            if (pane->shellId() == id)
                return session.get();

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

        // The same tree snapshot that session-restore persists doubles as
        // the client's layout: shellId is the pane id everywhere.
        for (const auto& node: session->view.snapshot())
        {
            auto layoutNode = LayoutNode {};
            layoutNode.split = node.split;
            layoutNode.horizontal = node.horizontal;
            layoutNode.ratio = node.ratio;
            layoutNode.first = node.first;
            layoutNode.second = node.second;
            layoutNode.pane = node.shellId;
            info.layout.push_back(layoutNode);
        }

        event.sessions.push_back(std::move(info));
    }

    return Miro::toJSONString(event);
}
} // namespace term::web
