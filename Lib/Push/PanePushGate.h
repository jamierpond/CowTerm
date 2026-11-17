#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Whether a pane's new state (`ct pane-state`) should ring the phone: only a
// Claude that finished or is asking, only when Jamie is not already looking
// at the pane, and once per event.
namespace term::push
{
enum class PushVerdict
{
    push,
    notFinishedOrWaiting,
    alreadyPushed,
    typedOnDesktopRecently,
    shownOnPhone,
};

struct PaneStateChange
{
    std::string state;
    std::string message;
    std::int64_t nowMilliseconds = 0;
};

struct PaneAttention
{
    // 0 when no desktop client ever typed into the pane.
    std::int64_t lastDesktopInputMilliseconds = 0;
    bool isPaneShownOnPhone = false;
};

class PanePushGate
{
public:
    PushVerdict reportStateChange(const PaneStateChange& change,
                                  const PaneAttention& attention);

private:
    struct PushedEvent
    {
        std::string state;
        std::string message;
    };

    bool isAlreadyPushed(const PaneStateChange& change) const;

    std::optional<PushedEvent> lastPushedEvent;
};

// One plain line for the daemon's log: "pushed", "not pushed: the phone shows
// the pane".
std::string_view describePushVerdict(PushVerdict verdict);
} // namespace term::push
