#pragma once

#include "NetworkPathWatch.h"

#include "Phone/PhoneConnection.h"
#include "Phone/SessionOpenings.h"
#include "TerminalView/TerminalView.h"

#include <functional>
#include <memory>
#include <optional>

namespace cowterm::mobile
{
// The phone's connection to its hosts: settings read from the launch arguments
// and SIMCTL_CHILD_ environment, or from the paired setup in the app's
// container, the stored last screen, and the hooks that keep the link honest
// (background, foreground, network change). Everything it hears is logged, and
// passed on to the pane screen.
class PhoneLaunch
{
public:
    explicit PhoneLaunch(term::phone::PhoneSettings settings);

    // From the launch arguments and environment when they name a destination,
    // else from the setup `just ios-pair` copied into the app's container.
    // Empty when neither has a host.
    static std::optional<term::phone::PhoneSettings> readSettings(int argc, char* argv[]);

    // The pane view's config: its shell is the daemon's pane on the host.
    term::TerminalConfig makeTerminalConfig();

    // dv for the pane on screen; see PhoneConnection::openDiffView.
    term::phone::DiffViewOpening openDiffView();

    // The dv pane's config: its command shell starts only dv, in the pane's folder.
    term::TerminalConfig makeDiffViewConfig(const term::phone::DiffViewPane& pane);

    void closeDiffView(const term::phone::DiffViewPane& pane);

    const std::string& getHost() const { return connection.getHost(); }

    // The pane to open at once; empty until the daemon has chosen one.
    std::string getPaneToOpen() const;

    // Another pane of the shown session; false when it is not one.
    bool choosePaneOfSession(const std::string& paneId);

    // The overview's rows from every host the launch named.
    term::phone::PhoneOverview& getOverview() { return connection.getOverview(); }

    // The pane to show for an overview row, or empty when its daemon lists
    // none of its panes; see PhoneConnection::openSession.
    std::optional<std::string> openSession(const term::overview::OverviewRow& row);

    // From Apps::setSuspendHandler.
    void handleSuspended(bool isSuspended);

    // For the pane screen, after the connection's own logging.
    std::function<void(term::phone::PhoneLinkState)> onLinkStateChanged =
        [](term::phone::PhoneLinkState) {};
    std::function<void(const term::phone::ChosenPane&)> onPaneChosen =
        [](const term::phone::ChosenPane&) {};

    // Sessions opened on the phone, which order the overview.
    term::phone::SessionOpenings& getSessionOpenings() { return sessionOpenings; }

    // What the last launch stored, loaded before the link is up.
    const std::optional<term::phone::StoredScreen>& getStoredScreen() const { return storedScreen; }

private:
    static std::string describeHostList(const term::phone::PhoneSettings& settings);
    void logWhatTheConnectionHears();

    term::phone::PhoneSettings settings;
    std::optional<term::phone::StoredScreen> storedScreen;
    term::phone::SessionOpenings sessionOpenings;
    term::phone::PhoneConnection connection;
    NetworkPathWatch networkPathWatch;
};
} // namespace cowterm::mobile
