#pragma once

#include <Miro/Reflect.h>

#include <optional>
#include <string>

// What a daemon tells the push relay when a Claude in one of its panes
// finished its turn or is asking something: the relay turns it into a push.
namespace term::push
{
constexpr auto finishedEvent = "finished";
constexpr auto waitingEvent = "waiting";

struct PushNotice
{
    std::string host;
    std::string session;
    std::string paneId;
    std::string event;
    std::string text;

    MIRO_REFLECT(host, session, paneId, event, text)
};

std::string writePushNoticeJson(const PushNotice& notice);

// Nullopt when the JSON lacks the host, the pane or a known event.
std::optional<PushNotice> readPushNoticeJson(const std::string& json);
} // namespace term::push
