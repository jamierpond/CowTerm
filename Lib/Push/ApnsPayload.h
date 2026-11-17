#pragma once

#include "PushNotice.h"

#include <string>

// The notification Apple delivers to the phone for a daemon's notice: the
// title is "host/session", the body Claude's line, grouped by session, with a
// sound. The phone reads the pane to open from the keys below.
namespace term::push
{
constexpr auto payloadHostKey = "host";
constexpr auto payloadSessionKey = "session";
constexpr auto payloadPaneKey = "pane";

std::string getNotificationTitle(const PushNotice& notice);

// The text when Claude gave none, cut to fit a lock screen and Apple's size limit.
std::string getNotificationBody(const PushNotice& notice);

std::string buildApnsPayloadJson(const PushNotice& notice, const std::string& soundName);
} // namespace term::push
