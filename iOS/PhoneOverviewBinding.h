#pragma once

#include "OverviewScreen.h"
#include "PhoneLaunch.h"

#include "Phone/PushTarget.h"

#include <functional>
#include <optional>
#include <string>

namespace cowterm::mobile
{
// Feeds the overview the hosts' real sessions and projects while it is shown,
// and opens a tapped session's pane: the one typed into last, else its first,
// only when its daemon still lists it. A tapped project gets a new session. The pane binding puts that pane on the pane
// screen; showPaneScreen then brings the pane screen up.
class PhoneOverviewBinding
{
public:
    PhoneOverviewBinding(PhoneLaunch& phoneToUse, eacp::Callback showPaneScreenToUse);

    // Call as a new overview comes on screen: it gets the sessions, and the
    // hosts are asked every few seconds until a session opens.
    void showOn(OverviewScreen& screenToUse);

    // Call before the overview is destroyed.
    void stopShowing();

    // Opens "<host>/<session>" or "<host>/<project>" the first time a report
    // lists it, through the same path as a tap: for a launch that cannot tap
    // (a headless simulator).
    void openSessionOnceListed(const std::string& hostAndSession);

    // Opens a tapped notification's pane once a report lists its session.
    void openNotificationTargetOnceListed(const term::phone::NotificationTarget& target);

private:
    void showReport(const term::phone::OverviewReport& report);
    void logHostsAnsweringWhenChanged(const term::phone::OverviewReport& report);
    void openSession(const term::overview::OverviewRow& row);
    std::optional<std::string> openRow(const term::overview::OverviewRow& row);
    void openNotificationTargetWhenListed(const term::phone::OverviewReport& report);

    PhoneLaunch& phone;
    std::optional<std::reference_wrapper<OverviewScreen>> screen;
    eacp::Callback showPaneScreen;
    std::string sessionToOpen;
    std::optional<term::phone::NotificationTarget> notificationTargetToOpen;
    std::string loggedHostsAnswering;
};

OverviewInputs makeOverviewInputs(const term::phone::OverviewReport& report,
                                  const term::phone::SessionOpenings& openings);
} // namespace cowterm::mobile
