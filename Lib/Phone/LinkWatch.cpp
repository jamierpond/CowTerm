#include "LinkWatch.h"

#include <array>

namespace term::phone
{
namespace
{
constexpr auto silenceBeforeStalled = Milliseconds {3000};
constexpr auto silenceBeforeRedial = Milliseconds {8000};
constexpr auto answerWaitAfterWaking = Milliseconds {2000};

struct LinkStateName
{
    PhoneLinkState state;
    std::string_view name;
};

constexpr auto linkStateNames = std::array {
    LinkStateName {PhoneLinkState::Connecting, "connecting"},
    LinkStateName {PhoneLinkState::Live, "live"},
    LinkStateName {PhoneLinkState::Stalled, "stalled"},
    LinkStateName {PhoneLinkState::Unreachable, "unreachable"},
};
} // namespace

void LinkWatch::noteBytesHeard(Milliseconds now)
{
    lastHeard = now;
    wokeUpAt.reset();
}

void LinkWatch::noteWokeUp(Milliseconds now)
{
    wokeUpAt = now;
}

void LinkWatch::noteReconnecting(Milliseconds now)
{
    lastHeard = now;
    wokeUpAt.reset();
}

PhoneLinkState LinkWatch::classify(RemoteHost::Status status, Milliseconds now) const
{
    if (status == RemoteHost::Status::Connecting)
        return PhoneLinkState::Connecting;

    if (status == RemoteHost::Status::Unreachable)
        return PhoneLinkState::Unreachable;

    if (hasBeenSilentFor(silenceBeforeStalled, now))
        return PhoneLinkState::Stalled;

    return PhoneLinkState::Live;
}

bool LinkWatch::shouldReconnect(RemoteHost::Status status, Milliseconds now) const
{
    if (status == RemoteHost::Status::Unreachable)
        return wokeUpAt.has_value();

    if (status == RemoteHost::Status::Connecting)
        return false;

    return hasBeenSilentFor(silenceBeforeRedial, now) || hasHeardNothingSinceWaking(now);
}

bool LinkWatch::hasBeenSilentFor(Milliseconds duration, Milliseconds now) const
{
    return now - lastHeard > duration;
}

bool LinkWatch::hasHeardNothingSinceWaking(Milliseconds now) const
{
    return wokeUpAt && now - *wokeUpAt > answerWaitAfterWaking;
}

std::string_view describeLinkState(PhoneLinkState state)
{
    for (const auto& entry: linkStateNames)
    {
        if (entry.state == state)
            return entry.name;
    }

    return {};
}
} // namespace term::phone
