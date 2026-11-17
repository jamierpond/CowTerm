#include "PushRelayServer.h"

namespace term::push
{
namespace
{
constexpr auto handlerThreadCount = 4;

eacp::HTTP::Response makeTextResponse(const RelayAnswer& answer)
{
    auto response = eacp::HTTP::Response {};
    response.statusCode = answer.statusCode;
    response.setContent(answer.text + "\n", "text/plain; charset=utf-8");
    return response;
}

eacp::HTTP::ServerOptions makeServerOptions(const std::string& address)
{
    auto options = eacp::HTTP::ServerOptions {};
    options.threading = eacp::HTTP::ServerThreadingMode::ThreadPool;
    options.threadPoolSize = handlerThreadCount;
    options.bindAddress = address;
    return options;
}
} // namespace

PushRelayServer::PushRelayServer(PushRelay& relayToServe)
    : relay(relayToServe)
{
}

void PushRelayServer::addRoutes(eacp::HTTP::Server& serverToRoute)
{
    serverToRoute.post("/devices",
                [this](const eacp::HTTP::Request& request)
                { return makeTextResponse(relay.registerDevice(request.body)); });

    serverToRoute.post("/notices",
                [this](const eacp::HTTP::Request& request)
                { return makeTextResponse(relay.deliverNotice(request.body)); });

    serverToRoute.get("/health",
               [](const eacp::HTTP::Request&)
               {
                   constexpr auto okStatus = 200;
                   return makeTextResponse({okStatus, "ok"});
               });
}

bool PushRelayServer::listen(const std::string& address, int port)
{
    server.emplace(makeServerOptions(address));
    addRoutes(*server);
    return server->listen(port);
}

int PushRelayServer::getBoundPort() const
{
    if (!server)
        return 0;

    return server->boundPort();
}
} // namespace term::push
