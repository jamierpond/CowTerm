#include "PaneReadState.h"

namespace term
{
bool PaneReadState::reportEvent(const UnreadEvent& event)
{
    if (isViewed())
    {
        unread.reset();
        return false;
    }

    if (isSameAsUnread(event))
        return false;

    unread = event;
    return true;
}

void PaneReadState::markRead()
{
    unread.reset();
}

void PaneReadState::startViewing(ViewerId viewer)
{
    viewers.insert(viewer);
    unread.reset();
}

void PaneReadState::stopViewing(ViewerId viewer)
{
    viewers.erase(viewer);
}

bool PaneReadState::isViewed() const
{
    return !viewers.empty();
}

void PaneReadState::restoreUnread(const UnreadEvent& kept)
{
    unread = kept;
}

bool PaneReadState::isSameAsUnread(const UnreadEvent& event) const
{
    return unread && unread->event == event.event && unread->text == event.text;
}
} // namespace term
