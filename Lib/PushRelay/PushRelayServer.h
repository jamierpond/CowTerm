#pragma once

#include "PushRelay.h"

#include <eacp/Network/HTTPServer/HttpServer.h>

#include <optional>
#include <string>

// The relay's HTTP endpoint, listening on one address (the relay host's
// tailnet address, so only Jamie's machines and phone reach it):
//   POST /devices  a phone registers its device token
//   POST /notices  a daemon hands in a Claude's finishing or asking
//   GET  /health   answers "ok"
namespace term::push
{
class PushRelayServer
{
public:
    explicit PushRelayServer(PushRelay& relayToServe);

    // False when the address is not one of this machine's or the port is taken.
    bool listen(const std::string& address, int port);
    int getBoundPort() const;

private:
    void addRoutes(eacp::HTTP::Server& serverToRoute);

    PushRelay& relay;
    std::optional<eacp::HTTP::Server> server;
};
} // namespace term::push
