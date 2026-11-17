#include "ApnsPayload.h"

namespace term::push
{
namespace
{
constexpr auto maximumBodyBytes = std::size_t {240};
constexpr auto ellipsis = "…";
constexpr auto finishedWithoutText = "Claude finished.";
constexpr auto waitingWithoutText = "Claude is waiting for you.";

struct ApnsAlert
{
    std::string title;
    std::string body;

    MIRO_REFLECT(title, body)
};

struct ApnsSystemFields
{
    ApnsAlert alert;
    std::string sound;
    std::string threadIdentifier;

    MIRO_REFLECT_MEMBERS(alert, "alert", sound, "sound", threadIdentifier, "thread-id")
};

struct ApnsPayload
{
    ApnsSystemFields aps;
    std::string host;
    std::string session;
    std::string pane;

    MIRO_REFLECT_MEMBERS(aps,
                         "aps",
                         host,
                         payloadHostKey,
                         session,
                         payloadSessionKey,
                         pane,
                         payloadPaneKey)
};

bool isUtf8ContinuationByte(char byte)
{
    const auto value = static_cast<unsigned char>(byte);
    constexpr auto continuationMask = 0xC0u;
    constexpr auto continuationBits = 0x80u;
    return (value & continuationMask) == continuationBits;
}

std::string cutToWholeCharacters(const std::string& text, std::size_t maximumBytes)
{
    if (text.size() <= maximumBytes)
        return text;

    auto end = maximumBytes;

    while (end > 0 && isUtf8ContinuationByte(text[end]))
        --end;

    return text.substr(0, end) + ellipsis;
}

std::string getTextWhenClaudeGaveNone(const PushNotice& notice)
{
    if (notice.event == waitingEvent)
        return waitingWithoutText;

    return finishedWithoutText;
}
} // namespace

std::string getNotificationTitle(const PushNotice& notice)
{
    if (notice.session.empty())
        return notice.host;

    return notice.host + "/" + notice.session;
}

std::string getNotificationBody(const PushNotice& notice)
{
    if (notice.text.empty())
        return getTextWhenClaudeGaveNone(notice);

    return cutToWholeCharacters(notice.text, maximumBodyBytes);
}

std::string buildApnsPayloadJson(const PushNotice& notice, const std::string& soundName)
{
    auto payload = ApnsPayload {};
    payload.aps.alert = {getNotificationTitle(notice), getNotificationBody(notice)};
    payload.aps.sound = soundName;
    payload.aps.threadIdentifier = getNotificationTitle(notice);
    payload.host = notice.host;
    payload.session = notice.session;
    payload.pane = notice.paneId;
    return Miro::toJSONString(payload);
}
} // namespace term::push
