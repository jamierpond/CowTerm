#pragma once

#include "OverviewScreen.h"
#include "PhoneLaunch.h"

#include <functional>
#include <string>

namespace cowterm::mobile
{
// Feeds the overview the hosts' real sessions while it is shown, and opens a
// tapped session's pane: the one typed into last, else its first, only when
// its daemon still lists it. The pane binding puts that pane on the pane
// screen; showPaneScreen then brings the pane screen up.
class PhoneOverviewBinding
{
public:
    PhoneOverviewBinding(PhoneLaunch& phoneToUse, OverviewScreen& screenToUse, eacp::Callback showPaneScreenToUse);

    // Call with true as the overview comes on screen: the hosts are asked
    // every few seconds until a session opens.
    void setShown(bool isShown);

    // Opens "<host>/<session>" the first time a report lists it, through the
    // same path as a tap: for a launch that cannot tap (a headless simulator).
    void openSessionOnceListed(const std::string& hostAndSession);

private:
    void showReport(const term::phone::OverviewReport& report);
    void logHostsAnsweringWhenChanged(const term::phone::OverviewReport& report);
    void openSession(const term::overview::OverviewRow& row);

    PhoneLaunch& phone;
    OverviewScreen& screen;
    eacp::Callback showPaneScreen;
    std::string sessionToOpen;
    std::string loggedHostsAnswering;
};

OverviewInputs makeOverviewInputs(const term::phone::OverviewReport& report);
} // namespace cowterm::mobile
