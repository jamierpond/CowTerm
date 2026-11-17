#pragma once

#include "Multiplexer/RemoteHosts.h"

#include <cstdint>
#include <optional>

namespace term::phone
{
using Milliseconds = std::int64_t;

// How the phone's link looks to the person holding it.
enum class PhoneLinkState
{
    Connecting,
    Live,
    Stalled,
    Unreachable
};

// Decides when a link that is up has gone quiet, and when to dial again
// without waiting for RemoteHost's backoff. A live daemon answers the info
// request DaemonClient sends every second, so silence means a stall.
class LinkWatch
{
public:
    void noteBytesHeard(Milliseconds now);

    // The app came back to the foreground, or the network changed: the link
    // may be dead without knowing it yet.
    void noteWokeUp(Milliseconds now);
    void noteReconnecting(Milliseconds now);

    PhoneLinkState classify(RemoteHost::Status status, Milliseconds now) const;
    bool shouldReconnect(RemoteHost::Status status, Milliseconds now) const;

private:
    bool hasBeenSilentFor(Milliseconds duration, Milliseconds now) const;
    bool hasHeardNothingSinceWaking(Milliseconds now) const;

    Milliseconds lastHeard = 0;
    std::optional<Milliseconds> wokeUpAt;
};

std::string_view describeLinkState(PhoneLinkState state);
} // namespace term::phone
