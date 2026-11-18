#pragma once

#include "PaneScreen.h"
#include "PhoneLaunch.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace cowterm::mobile
{
// Connects the phone's link to the pane screen: the link state and the age of
// the picture on the top line, the chosen pane's session and its dots, the
// unread marks on the bar, putting
// the chosen pane on the screen, and moving between the session's panes.
class PhonePaneBinding
{
public:
    // showPaneScreen brings the pane screen up once a pane is on it.
    PhonePaneBinding(PhoneLaunch& phoneToUse, PaneScreen& screenToUse, eacp::Callback showPaneScreenToUse);

    // A pane's config: the daemon's pane on host, drawn at once from the screen
    // the last launch stored for it, dimmed, until the daemon's state arrives.
    static term::TerminalConfig makePaneConfig(PhoneLaunch& phone, const term::phone::PaneOnHostId& pane);

private:
    void showLinkState(term::phone::PhoneLinkState state);
    void showChosenPane(const term::phone::ChosenPane& chosen);
    void switchToPane(int paneIndex);
    void showStoredScreenDescription();
    void showUnread(const term::phone::OverviewReport& report);

    PhoneLaunch& phone;
    PaneScreen& screen;
    eacp::Callback showPaneScreen;
    std::string shownPaneId;
    std::vector<std::string> sessionPaneIds;
    std::optional<term::phone::ChosenPane> chosenPane;
    term::phone::PaneScreenUnread unread;
    std::chrono::steady_clock::time_point pictureTakenAt = std::chrono::steady_clock::now();
    LinkState linkState = LinkState::Connecting;
};
} // namespace cowterm::mobile
