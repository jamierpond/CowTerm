#include "PhonePaneBinding.h"

#include "StoredScreenShell.h"

#include "Emulator/TermState.h"

#include <algorithm>

namespace cowterm::mobile
{
using namespace term::phone;

namespace
{
bool hasStoredScreenForFirstPane(const PhoneLaunch& phone)
{
    const auto& stored = phone.getStoredScreen();
    return stored && stored->paneId == phone.getPaneToOpen();
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

PaneDescription describeChosenPane(const ChosenPane& chosen)
{
    auto pane = PaneDescription {};
    pane.hostName = chosen.host;
    pane.sessionName = chosen.sessionName;
    pane.paneCount = std::max((int) chosen.sessionPaneIds.size(), 1);

    const auto found = std::ranges::find(chosen.sessionPaneIds, chosen.paneId);

    if (found != chosen.sessionPaneIds.end())
        pane.activePaneIndex = (int) std::distance(chosen.sessionPaneIds.begin(), found);

    return pane;
}
} // namespace

PhonePaneBinding::PhonePaneBinding(PhoneLaunch& phoneToUse, PaneScreen& screenToUse)
    : phone(phoneToUse)
    , screen(screenToUse)
{
    showStoredScreenDescription();

    phone.onLinkStateChanged = [this](PhoneLinkState state) { showLinkState(state); };
    phone.onPaneChosen = [this](const ChosenPane& chosen) { showChosenPane(chosen); };
    screen.actions.onChoosePane = [this](int paneIndex) { switchToPane(paneIndex); };
}

term::TerminalConfig PhonePaneBinding::makeFirstPaneConfig(PhoneLaunch& phone)
{
    auto config = phone.makeTerminalConfig();

    if (!hasStoredScreenForFirstPane(phone))
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

    if (hasStoredScreenForFirstPane(phone))
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

void PhonePaneBinding::showChosenPane(const ChosenPane& chosen)
{
    sessionPaneIds = chosen.sessionPaneIds;
    screen.setPaneDescription(describeChosenPane(chosen));
}

void PhonePaneBinding::switchToPane(int paneIndex)
{
    if (paneIndex < 0 || paneIndex >= (int) sessionPaneIds.size())
        return;

    const auto paneId = sessionPaneIds[(std::size_t) paneIndex];

    if (phone.choosePaneOfSession(paneId))
        screen.showPane(phone.makeTerminalConfig(), paneId);
}
} // namespace cowterm::mobile
