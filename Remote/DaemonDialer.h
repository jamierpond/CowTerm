#pragma once

#include <eacp/Network/IPC/Messenger.h>

#include <functional>
#include <memory>
#include <string>

namespace term::daemon
{
// The session daemon beside the running executable: cowtermd in a headless
// install, CowTermDaemon inside the app. Empty when neither is there.
std::string findDaemonExecutable();

// Starts it detached, so it outlives whoever launched it.
void launchDaemon();

using DialedDaemon = std::function<void(std::unique_ptr<eacp::IPC::Messenger>)>;

// Dials the daemon, launching it when nobody answers, and hands over the
// connected Messenger, or null when it stays unreachable. Main thread.
void dialOrLaunchDaemon(const DialedDaemon& whenDone);
} // namespace term::daemon
