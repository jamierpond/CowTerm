#include "PushNotice.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace term::push
{
namespace
{
constexpr auto unreadEvents = std::array {std::string_view {finishedUnreadEvent},
                                          std::string_view {waitingUnreadEvent},
                                          std::string_view {bellUnreadEvent},
                                          std::string_view {notificationUnreadEvent}};

bool isKnownEvent(const std::string& event)
{
    return std::ranges::find(unreadEvents, event) != unreadEvents.end();
}

bool isComplete(const PushNotice& notice)
{
    return !notice.host.empty() && !notice.paneId.empty() && isKnownEvent(notice.event);
}
} // namespace

std::string writePushNoticeJson(const PushNotice& notice)
{
    return Miro::toJSONString(notice);
}

std::optional<PushNotice> readPushNoticeJson(const std::string& json)
{
    const auto notice = Miro::createFromJSONString<PushNotice>(json);

    if (!isComplete(notice))
        return std::nullopt;

    return notice;
}
} // namespace term::push
