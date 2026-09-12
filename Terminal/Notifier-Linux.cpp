#include "Notifier.h"

#include <eacp/Core/Process/Process.h>

#include <utility>

// Desktop notifications go out through notify-send, the libnotify CLI every
// mainstream Linux desktop ships: it speaks org.freedesktop.Notifications over
// the session bus, so the notification is a real one rather than a window we
// draw. The child is launched detached — it outlives the Process object, which
// would otherwise kill it on destruction.
//
// Click-to-activate is not wired up here. Recovering the click would mean
// holding a D-Bus connection open for the ActionInvoked signal (or keeping a
// `notify-send --wait` child alive per notification), and neither belongs in a
// fire-and-forget post. The handler is stored so the contract is honoured the
// moment a bus listener is added; until then a click just shows the desktop's
// own notification behaviour.
namespace term::Notifier
{
namespace
{
std::function<void(const std::string&)> activateHandler =
    [](const std::string&) {};
} // namespace

void initialize(std::function<void(const std::string& sessionKey)> onActivate)
{
    if (onActivate)
        activateHandler = std::move(onActivate);
}

void notify(const std::string& sessionKey,
            const std::string& title,
            const std::string& body)
{
    static_cast<void>(sessionKey);

    auto options = eacp::Processes::ProcessOptions {};
    options.executable = "notify-send";
    options.arguments = {"--app-name=CowTerm", title, body};
    options.detached = true;

    // Constructed and dropped: a detached child is not owned by the object, so
    // the notification survives this scope.
    const auto process = eacp::Processes::Process {std::move(options)};
    static_cast<void>(process);
}
} // namespace term::Notifier
