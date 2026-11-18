#include "UnreadPanes.h"

#include <algorithm>

namespace
{
std::string flattenToOneLine(std::string text)
{
    std::ranges::replace(text, '\n', ' ');
    std::ranges::replace(text, '\t', ' ');
    return text;
}
} // namespace

UnreadPanes::UnreadPanes(emberstore::Document<term::UnreadPanes> storeToUse, PushRelayTarget pushRelayTarget)
    : store(std::move(storeToUse))
    , pushNotifier(std::move(pushRelayTarget))
{
    for (const auto& [paneId, kept]: store.read().panes)
        panes[paneId].restoreUnread(kept);
}

void UnreadPanes::reportEvent(const PaneForPush& pane, const term::UnreadEvent& event)
{
    const auto isNew = findPane(pane.paneId).reportEvent(event);
    save();

    if (isNew)
        pushNotifier.postNotice(pane, event);
}

void UnreadPanes::markRead(const std::string& paneId)
{
    findPane(paneId).markRead();
    save();
}

void UnreadPanes::startViewing(const std::string& paneId, term::PaneReadState::ViewerId viewer)
{
    findPane(paneId).startViewing(viewer);
    save();
}

void UnreadPanes::stopViewing(const std::string& paneId, term::PaneReadState::ViewerId viewer)
{
    findPane(paneId).stopViewing(viewer);
}

void UnreadPanes::stopViewingEverything(term::PaneReadState::ViewerId viewer)
{
    for (auto& [paneId, pane]: panes)
        pane.stopViewing(viewer);
}

void UnreadPanes::forgetPane(const std::string& paneId)
{
    panes.erase(paneId);
    save();
}

std::string UnreadPanes::describeForActivity(const std::string& paneId) const
{
    const auto found = panes.find(paneId);

    if (found == panes.end() || !found->second.getUnread())
        return "\t\t";

    const auto& unread = *found->second.getUnread();
    return unread.event + "\t" + std::to_string(unread.sinceMilliseconds) + "\t"
           + flattenToOneLine(unread.text);
}

term::PaneReadState& UnreadPanes::findPane(const std::string& paneId)
{
    return panes[paneId];
}

void UnreadPanes::save()
{
    auto kept = term::UnreadPanes {};

    for (const auto& [paneId, pane]: panes)
    {
        if (const auto& unread = pane.getUnread())
            kept.panes[paneId] = *unread;
    }

    store.write(kept);
}
