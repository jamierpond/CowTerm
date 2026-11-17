#pragma once

#include "PaneScreen.h"
#include "PhoneLaunch.h"

#include <chrono>
#include <string>
#include <vector>

namespace cowterm::mobile
{
// Connects the phone's link to the pane screen: the link state and the age of
// the picture on the top line, the chosen pane's session and its dots, and
// moving between the session's panes.
class PhonePaneBinding
{
public:
    PhonePaneBinding(PhoneLaunch& phoneToUse, PaneScreen& screenToUse);

    // The first pane's config: the daemon's pane, drawn at once from the screen
    // the last launch stored for it, dimmed, until the daemon's state arrives.
    static term::TerminalConfig makeFirstPaneConfig(PhoneLaunch& phone);

private:
    void showLinkState(term::phone::PhoneLinkState state);
    void showChosenPane(const term::phone::ChosenPane& chosen);
    void switchToPane(int paneIndex);
    void showStoredScreenDescription();

    PhoneLaunch& phone;
    PaneScreen& screen;
    std::vector<std::string> sessionPaneIds;
    std::chrono::steady_clock::time_point pictureTakenAt = std::chrono::steady_clock::now();
    LinkState linkState = LinkState::Connecting;
};
} // namespace cowterm::mobile
