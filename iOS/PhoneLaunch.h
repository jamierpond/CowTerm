#pragma once

#include "NetworkPathWatch.h"

#include "Phone/PhoneConnection.h"
#include "TerminalView/TerminalView.h"

#include <functional>
#include <memory>
#include <optional>

namespace cowterm::mobile
{
// The phone's connection to a host, when the launch named one: settings read
// from the launch arguments and SIMCTL_CHILD_ environment, the stored last
// screen, and the hooks that keep the link honest (background, foreground,
// network change). Everything it hears is logged, and passed on to the pane screen.
class PhoneLaunch
{
public:
    explicit PhoneLaunch(term::phone::PhoneSettings settings);

    // Empty when the launch names no destination: the app shows sample text.
    static std::optional<term::phone::PhoneSettings> readSettings(int argc, char* argv[]);

    // The pane view's config: its shell is the daemon's pane on the host.
    term::TerminalConfig makeTerminalConfig();

    const std::string& getHost() const { return connection.getHost(); }

    // The pane to open at once; empty until the daemon has chosen one.
    std::string getPaneToOpen() const;

    // Another pane of the shown session; false when it is not one.
    bool choosePaneOfSession(const std::string& paneId);

    // From Apps::setSuspendHandler.
    void handleSuspended(bool isSuspended);

    // For the pane screen, after the connection's own logging.
    std::function<void(term::phone::PhoneLinkState)> onLinkStateChanged =
        [](term::phone::PhoneLinkState) {};
    std::function<void(const term::phone::ChosenPane&)> onPaneChosen =
        [](const term::phone::ChosenPane&) {};

    // What the last launch stored, loaded before the link is up.
    const std::optional<term::phone::StoredScreen>& getStoredScreen() const { return storedScreen; }

private:
    void logWhatTheConnectionHears();

    term::phone::PhoneSettings settings;
    std::optional<term::phone::StoredScreen> storedScreen;
    term::phone::PhoneConnection connection;
    NetworkPathWatch networkPathWatch;
};
} // namespace cowterm::mobile
