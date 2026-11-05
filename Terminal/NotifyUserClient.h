#pragma once

#include "NotifyUserProtocol.h"

#include <functional>

namespace term
{
// How a `ct notify-user` ask ended: the reply, or no reply because the app
// could not be reached or went away before answering.
struct NotifyUserOutcome
{
    bool wasAnswered = false;
    NotifyUserReply reply;
};

// Dials the running app, sends the request and waits for the user, running an
// event loop until then. Returns the exit code exitCodeForOutcome chose.
int askRunningAppAndWait(const NotifyUserRequest& request,
                         std::function<int(const NotifyUserOutcome&)> exitCodeForOutcome);
} // namespace term
