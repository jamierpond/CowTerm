#pragma once

#include "Multiplexer/PaneReadState.h"

#include <Miro/Reflect.h>

#include <optional>
#include <string>

// What a daemon tells the push relay when one of its panes has a new unread
// event (PaneReadState): the relay turns it into a push.
namespace term::push
{
struct PushNotice
{
    std::string host;
    std::string session;
    std::string paneId;
    // One of the unread events: finished, waiting, bell, notification.
    std::string event;
    std::string text;

    MIRO_REFLECT(host, session, paneId, event, text)
};

std::string writePushNoticeJson(const PushNotice& notice);

// Nullopt when the JSON lacks the host, the pane or a known event.
std::optional<PushNotice> readPushNoticeJson(const std::string& json);
} // namespace term::push
