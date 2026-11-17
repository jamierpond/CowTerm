#include "PushNotice.h"

namespace term::push
{
namespace
{
bool isKnownEvent(const std::string& event)
{
    return event == finishedEvent || event == waitingEvent;
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
