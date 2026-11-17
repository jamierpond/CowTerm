#pragma once

#include "LinkWatch.h"
#include "MirroredShell.h"
#include "PaneChoice.h"
#include "PhoneSettings.h"
#include "StoredScreen.h"

#include <eacp/Core/Threads/Timer.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace term::phone
{
// The pane the phone shows, and the panes of its session in order.
struct ChosenPane
{
    std::string host;
    std::string paneId;
    std::string sessionName;
    std::vector<std::string> sessionPaneIds;
};

// The phone's one link to one host's daemon, through EmbeddedSshLink: it
// decides which pane to open, reports how the link is doing, keeps the shown
// pane's last screen on disk and dials again when the app wakes or the network
// changes. Built on RemoteHosts, DaemonClient and makeRemoteShell, exactly as
// the Mac reaches a remote host. Main-thread object; one per process, since
// RemoteHosts is.
class PhoneConnection
{
public:
    explicit PhoneConnection(PhoneSettings settingsToUse);
    ~PhoneConnection();

    // The shell for the pane view (TerminalConfig::makeShell): the daemon's
    // pane, mirrored so its screen can be stored.
    std::unique_ptr<Shell> makeShell(const std::string& paneId);

    const std::string& getHost() const { return host; }
    PhoneLinkState getLinkState() const { return linkState; }
    std::string getFailureReason() const;
    const std::optional<ChosenPane>& getChosenPane() const { return chosenPane; }

    // The app went to the background: store the shown screen now.
    void handleBackground();

    // The app is back, or the network path changed: check the link at once
    // instead of waiting for it to time out.
    void handleWake();

    std::function<void(PhoneLinkState)> onLinkStateChanged = [](PhoneLinkState) {};
    std::function<void(const ChosenPane&)> onPaneChosen = [](const ChosenPane&) {};

    // A note the pane shows over its text: the link was lost, or another
    // device holds the pane's size ("on Mac, 150 columns; ...").
    std::function<void(const std::string&)> onPaneStatus = [](const std::string&) {};

private:
    RemoteHost& getRemoteHost() const;
    std::unique_ptr<BridgeLink> makeLink();
    void handleHostChanged();
    void handleUnhandledMessage(const std::string& verb, const std::string& body);
    void handleTick();
    bool isWaitingToAskForLastInput() const;
    bool hasGivenUpOnLastInput(Milliseconds now) const;
    void storeShownScreenWhileLive(Milliseconds now);
    void askForLastInput();
    void choosePaneWhenKnown();
    std::vector<ListedPane> listPanes() const;
    void storeShownScreen();
    void updateLinkState();

    PhoneSettings settings;
    std::string host;
    LinkWatch watch;
    PhoneLinkState linkState = PhoneLinkState::Connecting;
    std::shared_ptr<PaneMirror> mirror = std::make_shared<PaneMirror>();
    std::optional<ChosenPane> chosenPane;
    std::optional<std::string> lastInputPane;
    std::optional<Milliseconds> askedForLastInputAt;
    bool hasLastInputAnswer = false;
    std::string shownPaneId;
    Milliseconds lastStoredAt = 0;
    std::unique_ptr<eacp::Threads::Timer> tickTimer;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

Milliseconds getMillisecondsSinceEpoch();
} // namespace term::phone
