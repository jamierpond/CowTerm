#pragma once

#include "Multiplexer/SessionOverview.h"

#include <map>
#include <optional>
#include <string>

// The phone's side of a push: what it tells the relay when it registers, and
// which overview row a tapped notification opens.
namespace term::phone
{
// The pane a notification is about, from the keys the relay put in its payload.
struct NotificationTarget
{
    // As the daemon names its machine ("macprorack"), without a user.
    std::string host;
    std::string session;
    std::string paneId;
};

std::optional<NotificationTarget> readNotificationTarget(const std::map<std::string, std::string>& userInfo);

// "jamiepond@macprorack" is "macprorack": the overview names hosts by user and host.
std::string getHostNameOfLabel(const std::string& hostLabel);

// The row on the notification's host that holds its pane, else the one with its session's name.
std::optional<overview::OverviewRow> findRowForNotification(const std::vector<overview::OverviewRow>& rows,
                                                            const NotificationTarget& target);

// What POST /devices on the relay takes.
std::string makeDeviceRegistrationJson(const std::string& deviceName,
                                       const std::string& deviceToken,
                                       const std::string& environment);
} // namespace term::phone
