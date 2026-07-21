#include "GatewayClient.h"

#include <eacp/Core/Core.h>
#include <eacp/Network/HTTP/Http.h>

#include <Miro/Reflect.h>

#include <thread>

namespace term::web
{
using namespace eacp;
using namespace wire;

namespace
{
// Timer intervals are Hz: check the link once a second (dial() is a no-op
// while connected or mid-dial, so the tick is a flag test).
constexpr int redialHz = 1;

// The configured address is the remote's HTTP port; the websocket lives
// wherever /api/v1/server says it does — the same discovery the browser
// client performs, never an assumption about port layout. Returns 0 when
// the remote is unreachable or isn't a CowTerm gateway.
std::uint16_t discoverWsPort(const std::string& host, std::uint16_t httpPort)
{
    try
    {
        const auto response =
            HTTP::Request {"http://" + host + ":" + std::to_string(httpPort)
                           + "/api/v1/server"}
                .perform();

        if (response.statusCode != 200)
            return 0;

        auto info = wire::ServerInfo {};
        Miro::fromJSONString(info, response.content);

        // "ws://host:port/"
        const auto colon = info.wsUrl.rfind(':');

        if (colon == std::string::npos)
            return 0;

        return (std::uint16_t) std::atoi(info.wsUrl.c_str() + colon + 1);
    }
    catch (const std::exception&)
    {
        return 0;
    }
}
} // namespace

GatewayClient::GatewayClient(const std::string& addressToUse)
    : host(addressToUse)
    , displayAddress(addressToUse)
{
    if (const auto colon = host.rfind(':'); colon != std::string::npos)
    {
        const auto parsed = std::atoi(host.c_str() + colon + 1);

        if (parsed > 0)
        {
            port = (std::uint16_t) parsed;
            host = host.substr(0, colon);
        }
    }

    dial();

    redial = std::make_unique<Threads::Timer>(
        [this]
        {
            if (!connected && !dialing)
                dial();
        },
        redialHz);
}

GatewayClient::~GatewayClient()
{
    *alive = false;

    if (connection != nullptr)
        connection->close();
}

const PaneInfo* GatewayClient::findPane(const std::string& paneId) const
{
    for (const auto& session: model)
        for (const auto& pane: session.panes)
            if (pane.id == paneId)
                return &pane;

    return nullptr;
}

void GatewayClient::dial()
{
    dialing = true;

    // The connect blocks up to its timeout, so it lives on a throwaway
    // thread; only the outcome crosses back to the main loop.
    std::thread(
        [guard = std::weak_ptr<bool> {alive},
         this,
         dialHost = host,
         dialPort = port]
        {
            auto socket = WsConnection::Ptr {};

            try
            {
                if (const auto wsPort = discoverWsPort(dialHost, dialPort))
                    socket = wsConnectClient(dialHost, wsPort);
            }
            catch (const TCP::Error&)
            {
            }

            Threads::callAsync(
                [guard, this, socket]
                {
                    if (guard.expired())
                    {
                        if (socket != nullptr)
                            socket->close();

                        return;
                    }

                    dialing = false;

                    if (socket != nullptr)
                        adopt(socket);
                });
        })
        .detach();
}

void GatewayClient::adopt(const WsConnection::Ptr& socket)
{
    auto guard = std::weak_ptr<bool> {alive};

    connection = socket;
    connected = true;

    socket->onText = [guard, this](const std::string& body)
    {
        Threads::callAsync(
            [guard, this, body]
            {
                if (!guard.expired())
                    handleEvent(body);
            });
    };

    // Pane output skips the main-loop hop on purpose: Shell output is
    // documented any-thread, and the terminal marshals for itself.
    socket->onBinary = [guard, this](const std::string& frame)
    {
        if (!guard.expired())
            handleBinary(frame);
    };

    socket->onClosed = [guard, this]
    {
        Threads::callAsync(
            [guard, this]
            {
                if (guard.expired())
                    return;

                connected = false;
                connection.reset();
                onChanged();
            });
    };

    socket->start();

    // Panes attached before (or during) an outage pick their streams back
    // up; every attach opens with a fresh snapshot, so no bytes are owed.
    {
        auto lock = std::scoped_lock {routesLock};

        for (const auto& [paneId, paneRoutes]: routes)
            sendOp({.op = "attach", .pane = paneId});
    }

    onChanged();
}

void GatewayClient::handleEvent(const std::string& body)
{
    auto event = AnyEvent {};

    try
    {
        Miro::fromJSONString(event, body);
    }
    catch (const std::exception&)
    {
        return;
    }

    if (event.ev == "sessions")
    {
        model = std::move(event.sessions);
        onChanged();
    }
    else if (event.ev == "attached")
    {
        auto lock = std::scoped_lock {routesLock};

        if (auto found = routes.find(event.pane); found != routes.end())
            found->second.onSize(event.cols, event.rows);
    }
    else if (event.ev == "exit")
    {
        auto exited = PaneRoutes {};

        {
            auto lock = std::scoped_lock {routesLock};

            if (auto found = routes.find(event.pane); found != routes.end())
            {
                exited = std::move(found->second);
                routes.erase(found);
            }
        }

        exited.onExit();
    }
}

void GatewayClient::handleBinary(const std::string& frame)
{
    const auto parsed = parseBinaryFrame(frame);

    if (parsed.pane.empty())
        return;

    auto lock = std::scoped_lock {routesLock};

    if (auto found = routes.find(parsed.pane); found != routes.end())
        found->second.onOutput(std::string {parsed.payload});
}

void GatewayClient::sendOp(const ClientOp& op)
{
    if (connection != nullptr && connection->isOpen())
        connection->sendText(Miro::toJSONString(op));
}

void GatewayClient::attach(const std::string& paneId, PaneRoutes paneRoutes)
{
    {
        auto lock = std::scoped_lock {routesLock};
        routes[paneId] = std::move(paneRoutes);
    }

    sendOp({.op = "attach", .pane = paneId});
}

void GatewayClient::detach(const std::string& paneId)
{
    {
        auto lock = std::scoped_lock {routesLock};
        routes.erase(paneId);
    }

    sendOp({.op = "detach", .pane = paneId});
}

void GatewayClient::input(const std::string& paneId, std::string_view data)
{
    sendOp({.op = "input", .pane = paneId, .data = std::string {data}});
}

void GatewayClient::activate(const std::string& sessionKey)
{
    sendOp({.op = "activate", .key = sessionKey});
}

void GatewayClient::open(const std::string& dir)
{
    sendOp({.op = "open", .dir = dir});
}

RemoteShell::RemoteShell(GatewayClient& clientToUse, std::string paneIdToUse)
    : client(clientToUse)
    , paneId(std::move(paneIdToUse))
{
    if (const auto* pane = client.findPane(paneId))
        remoteSize = {pane->cols, pane->rows};
}

bool RemoteShell::start(const PtyOptions&,
                        std::function<void(std::string)> onOutput,
                        std::function<void()> onExit)
{
    client.attach(paneId,
                  {.onOutput = std::move(onOutput),
                   .onExit = std::move(onExit),
                   .onSize = [this](int cols, int rows)
                   { remoteSize = {cols, rows}; }});
    return true;
}

void RemoteShell::write(std::string_view data)
{
    client.input(paneId, data);
}

void RemoteShell::resize(const PtySize&)
{
    // The remote GUI owns its grid; this side only mirrors.
}

std::optional<PtySize> RemoteShell::fixedSize() const
{
    if (remoteSize.cols > 0 && remoteSize.rows > 0)
        return remoteSize;

    return {};
}

std::string RemoteShell::foregroundProcess() const
{
    const auto* pane = client.findPane(paneId);
    return pane != nullptr ? pane->title : std::string {};
}

std::string RemoteShell::currentWorkingDirectory() const
{
    const auto* pane = client.findPane(paneId);
    return pane != nullptr ? pane->cwd : std::string {};
}

void RemoteShell::terminate()
{
    // Letting go, never killing: the pane belongs to the remote machine.
    client.detach(paneId);
}

void RemoteShell::detach()
{
    client.detach(paneId);
}
} // namespace term::web
