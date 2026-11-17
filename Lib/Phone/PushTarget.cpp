#include "PushTarget.h"

#include <Push/ApnsPayload.h>

#include <Miro/Reflect.h>

#include <algorithm>

namespace term::phone
{
namespace
{
struct DeviceRegistration
{
    std::string name;
    std::string token;
    std::string environment;

    MIRO_REFLECT(name, token, environment)
};

std::string findValue(const std::map<std::string, std::string>& userInfo, const std::string& key)
{
    if (const auto found = userInfo.find(key); found != userInfo.end())
        return found->second;

    return {};
}

bool isOnHost(const overview::OverviewRow& row, const NotificationTarget& target)
{
    return getHostNameOfLabel(row.host) == target.host;
}

bool holdsPane(const overview::OverviewRow& row, const std::string& paneId)
{
    return std::ranges::find(row.paneIds, paneId) != row.paneIds.end();
}
} // namespace

std::optional<NotificationTarget> readNotificationTarget(const std::map<std::string, std::string>& userInfo)
{
    auto target = NotificationTarget {findValue(userInfo, push::payloadHostKey),
                                      findValue(userInfo, push::payloadSessionKey),
                                      findValue(userInfo, push::payloadPaneKey)};

    if (target.host.empty() || target.paneId.empty())
        return std::nullopt;

    return target;
}

std::string getHostNameOfLabel(const std::string& hostLabel)
{
    const auto atSign = hostLabel.find('@');
    const auto hostStart = atSign == std::string::npos ? 0 : atSign + 1;
    const auto portColon = hostLabel.find(':', hostStart);
    return hostLabel.substr(hostStart, portColon - hostStart);
}

std::optional<overview::OverviewRow> findRowForNotification(const std::vector<overview::OverviewRow>& rows,
                                                            const NotificationTarget& target)
{
    const auto byPane = std::ranges::find_if(
        rows, [&](const auto& row) { return isOnHost(row, target) && holdsPane(row, target.paneId); });

    if (byPane != rows.end())
        return *byPane;

    const auto bySession = std::ranges::find_if(
        rows, [&](const auto& row) { return isOnHost(row, target) && row.sessionName == target.session; });

    if (bySession != rows.end())
        return *bySession;

    return std::nullopt;
}

std::string makeDeviceRegistrationJson(const std::string& deviceName,
                                       const std::string& deviceToken,
                                       const std::string& environment)
{
    return Miro::toJSONString(DeviceRegistration {deviceName, deviceToken, environment});
}
} // namespace term::phone
