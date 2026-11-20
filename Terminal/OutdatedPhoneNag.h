#pragma once

#include "Multiplexer/PhoneBuildStamp.h"

#include <eacp/Core/Threads/Timer.h>

#include <functional>
#include <string>

namespace term
{
// Asks every daemon this Mac talks to which clients it has, and posts a
// desktop notification for a phone running an older build than this app.
class OutdatedPhoneNag
{
public:
    static constexpr auto clientsVerb = "clients";

    OutdatedPhoneNag(std::optional<PhoneBuildStamp> thisBuild,
                     std::function<void(const std::string& message)> sendToEveryDaemonToUse);

    // A daemon's "clients" answer; host is empty for this Mac's own daemon.
    void takeConnectedClients(const std::string& host, const std::string& body);

private:
    std::function<void(const std::string& message)> sendToEveryDaemon;
    OutdatedPhoneNotices notices;
    eacp::Threads::Timer askTimer;
};
} // namespace term
