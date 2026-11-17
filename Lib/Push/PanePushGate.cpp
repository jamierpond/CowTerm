#include "PanePushGate.h"
#include "PushNotice.h"

#include <algorithm>
#include <array>
#include <chrono>

namespace term::push
{
namespace
{
constexpr auto desktopInputQuietPeriod = std::chrono::milliseconds {std::chrono::minutes {1}};
constexpr auto workingState = "working";

struct PushVerdictDescription
{
    PushVerdict verdict;
    std::string_view description;
};

constexpr auto pushVerdictDescriptions = std::array {
    PushVerdictDescription {PushVerdict::push, "pushed"},
    PushVerdictDescription {PushVerdict::notFinishedOrWaiting, "not pushed: Claude is working"},
    PushVerdictDescription {PushVerdict::alreadyPushed, "not pushed: the same event was pushed already"},
    PushVerdictDescription {PushVerdict::typedOnDesktopRecently,
                            "not pushed: a desktop client typed into the pane in the last minute"},
    PushVerdictDescription {PushVerdict::shownOnPhone, "not pushed: the phone shows the pane"},
};

bool isFinishedOrWaiting(const std::string& state)
{
    return state == finishedEvent || state == waitingEvent;
}

bool hasDesktopTypedRecently(const PaneStateChange& change, const PaneAttention& attention)
{
    if (attention.lastDesktopInputMilliseconds == 0)
        return false;

    const auto sinceDesktopInput = change.nowMilliseconds - attention.lastDesktopInputMilliseconds;
    return sinceDesktopInput < desktopInputQuietPeriod.count();
}
} // namespace

PushVerdict PanePushGate::reportStateChange(const PaneStateChange& change,
                                            const PaneAttention& attention)
{
    if (change.state == workingState)
        lastPushedEvent.reset();

    if (!isFinishedOrWaiting(change.state))
        return PushVerdict::notFinishedOrWaiting;

    if (isAlreadyPushed(change))
        return PushVerdict::alreadyPushed;

    if (attention.isPaneShownOnPhone)
        return PushVerdict::shownOnPhone;

    if (hasDesktopTypedRecently(change, attention))
        return PushVerdict::typedOnDesktopRecently;

    lastPushedEvent = PushedEvent {change.state, change.message};
    return PushVerdict::push;
}

bool PanePushGate::isAlreadyPushed(const PaneStateChange& change) const
{
    return lastPushedEvent && lastPushedEvent->state == change.state
           && lastPushedEvent->message == change.message;
}

std::string_view describePushVerdict(PushVerdict verdict)
{
    const auto found =
        std::ranges::find(pushVerdictDescriptions, verdict, &PushVerdictDescription::verdict);
    return found->description;
}
} // namespace term::push
