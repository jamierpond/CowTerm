#include "PhonePaneBinding.h"

#include "StoredScreenShell.h"

#include "Emulator/TermState.h"

#include <algorithm>

namespace cowterm::mobile
{
using namespace term::phone;

namespace
{
bool hasStoredScreenFor(const PhoneLaunch& phone, const PaneOnHostId& pane)
{
    const auto& stored = phone.getStoredScreen();
    return stored && PaneOnHostId {stored->host, stored->paneId} == pane;
}

// The stored screen's save time, on the clock the top line counts the age on.
std::chrono::steady_clock::time_point findWhenStoredScreenWasSaved(const StoredScreen& stored)
{
    const auto ageMilliseconds = getMillisecondsSinceEpoch() - stored.savedAtMillisecondsSinceEpoch;
    return std::chrono::steady_clock::now() - std::chrono::milliseconds {ageMilliseconds};
}

LinkState toPaneScreenLinkState(PhoneLinkState state)
{
    switch (state)
    {
        case PhoneLinkState::Live:
            return LinkState::Live;
        case PhoneLinkState::Connecting:
            return LinkState::Connecting;
        case PhoneLinkState::Stalled:
            return LinkState::Stalled;
        case PhoneLinkState::Unreachable:
            return LinkState::Unreachable;
    }

    return LinkState::Connecting;
}

bool isSameSession(const ChosenPane& first, const ChosenPane& second)
{
    return first.host == second.host && first.sessionName == second.sessionName;
}

std::vector<int> findUnreadPaneIndexes(const ChosenPane& chosen, const PaneScreenUnread& unread)
{
    auto indexes = std::vector<int> {};

    for (auto index = 0; index < (int) chosen.sessionPaneIds.size(); ++index)
    {
        if (std::ranges::find(unread.unreadPaneIds, chosen.sessionPaneIds[(std::size_t) index]) != unread.unreadPaneIds.end())
            indexes.push_back(index);
    }

    return indexes;
}

PaneDescription describeChosenPane(const ChosenPane& chosen, const PaneScreenUnread& unread)
{
    auto pane = PaneDescription {};
    pane.hostName = chosen.host;
    pane.sessionName = chosen.sessionName;
    pane.paneCount = std::max((int) chosen.sessionPaneIds.size(), 1);

    const auto found = std::ranges::find(chosen.sessionPaneIds, chosen.paneId);

    if (found != chosen.sessionPaneIds.end())
        pane.activePaneIndex = (int) std::distance(chosen.sessionPaneIds.begin(), found);

    pane.unreadSessionsElsewhere = unread.unreadSessionsElsewhere;
    pane.unreadPaneIndexes = findUnreadPaneIndexes(chosen, unread);
    return pane;
}
} // namespace

PhonePaneBinding::PhonePaneBinding(PhoneLaunch& phoneToUse,
                                   PaneScreen& screenToUse,
                                   eacp::Callback showPaneScreenToUse)
    : phone(phoneToUse)
    , screen(screenToUse)
    , showPaneScreen(std::move(showPaneScreenToUse))
    , shownPaneId(phone.getPaneToOpen())
{
    showStoredScreenDescription();

    phone.onLinkStateChanged = [this](PhoneLinkState state) { showLinkState(state); };
    phone.onPaneChosen = [this](const ChosenPane& chosen) { showChosenPane(chosen); };
    screen.actions.onChoosePane = [this](int paneIndex) { switchToPane(paneIndex); };
    phone.getOverview().onRowsChangedUnderPaneScreen = [this](const OverviewReport& report) { showUnread(report); };
}

void PhonePaneBinding::showUnread(const OverviewReport& report)
{
    if (!chosenPane)
        return;

    unread = summarisePaneScreenUnread(report.sessions, chosenPane->host, chosenPane->sessionName);
    screen.setPaneDescription(describeChosenPane(*chosenPane, unread));
}

term::TerminalConfig PhonePaneBinding::makePaneConfig(PhoneLaunch& phone, const PaneOnHostId& pane)
{
    auto config = phone.makeTerminalConfig();

    if (!hasStoredScreenFor(phone, pane))
        return config;

    const auto serialized = term::serializeTerminalState(phone.getStoredScreen()->state);
    config.makeShell = [makePaneShell = config.makeShell, serialized](const std::string& paneId)
    { return std::make_unique<StoredScreenShell>(makePaneShell(paneId), serialized); };
    return config;
}

// Until the link is up the pane shows the stored screen, so the top line names
// its session and counts its age from when it was saved.
void PhonePaneBinding::showStoredScreenDescription()
{
    auto pane = PaneDescription {};
    pane.hostName = phone.getHost();

    if (hasStoredScreenFor(phone, {phone.getHost(), phone.getPaneToOpen()}))
    {
        const auto& stored = *phone.getStoredScreen();
        pane.sessionName = stored.sessionName;
        pictureTakenAt = findWhenStoredScreenWasSaved(stored);
    }

    screen.setPaneDescription(pane);
    screen.setLinkStatus({linkState, pictureTakenAt});
}

// A picture stays as it was when the link stopped being live, so its age
// counts from then.
void PhonePaneBinding::showLinkState(PhoneLinkState state)
{
    const auto wasLive = linkState == LinkState::Live;
    linkState = toPaneScreenLinkState(state);

    if (wasLive || linkState == LinkState::Live)
        pictureTakenAt = std::chrono::steady_clock::now();

    screen.setLinkStatus({linkState, pictureTakenAt});
}

// The launch's own choice, or a row opened from the overview, is put on the
// screen here; a pane already shown only gets its session's dots updated.
void PhonePaneBinding::showChosenPane(const ChosenPane& chosen)
{
    sessionPaneIds = chosen.sessionPaneIds;

    if (!chosenPane || !isSameSession(*chosenPane, chosen))
        unread.unreadPaneIds.clear();

    chosenPane = chosen;

    if (chosen.paneId != shownPaneId)
    {
        shownPaneId = chosen.paneId;
        screen.showPane(makePaneConfig(phone, {chosen.host, chosen.paneId}), chosen.paneId);
        showPaneScreen();
    }

    screen.setPaneDescription(describeChosenPane(chosen, unread));
}

void PhonePaneBinding::switchToPane(int paneIndex)
{
    if (paneIndex < 0 || paneIndex >= (int) sessionPaneIds.size())
        return;

    const auto paneId = sessionPaneIds[(std::size_t) paneIndex];
    phone.choosePaneOfSession(paneId);
}
} // namespace cowterm::mobile
