#pragma once

#include <Miro/Reflect.h>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>

// Whether Jamie has seen what a pane last said: a Claude that finished or
// asked, or a shell that rang its bell or sent a notification, while no client
// was showing it to him. A new unread event is also exactly what rings his phone.
namespace term
{
constexpr auto finishedUnreadEvent = "finished";
constexpr auto waitingUnreadEvent = "waiting";
constexpr auto bellUnreadEvent = "bell";
constexpr auto notificationUnreadEvent = "notification";

struct UnreadEvent
{
    std::string event;
    std::string text;
    std::int64_t sinceMilliseconds = 0;

    MIRO_REFLECT(event, text, sinceMilliseconds)
};

class PaneReadState
{
public:
    using ViewerId = std::uint64_t;

    // True when this is a new unread event, the one a push announces. An
    // event while a client views the pane is seen as it happens; the same
    // event again while still unread is not new.
    bool reportEvent(const UnreadEvent& event);

    void markRead();

    // A client shows the pane to a person now; it is read until they stop.
    void startViewing(ViewerId viewer);
    void stopViewing(ViewerId viewer);
    bool isViewed() const;

    const std::optional<UnreadEvent>& getUnread() const { return unread; }

    // What the daemon kept for this pane before it restarted.
    void restoreUnread(const UnreadEvent& kept);

private:
    bool isSameAsUnread(const UnreadEvent& event) const;

    std::optional<UnreadEvent> unread;
    std::set<ViewerId> viewers;
};

// Every unread pane of a daemon, as it keeps them on disk.
struct UnreadPanes
{
    std::map<std::string, UnreadEvent> panes;

    MIRO_REFLECT(panes)
};
} // namespace term
