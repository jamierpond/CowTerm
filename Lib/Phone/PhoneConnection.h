#pragma once

#include "DiffViewPane.h"
#include "HeldPane.h"
#include "LinkWatch.h"
#include "MirroredShell.h"
#include "PaneChoice.h"
#include "PhoneOverview.h"
#include "PhoneSettings.h"
#include "ProjectSession.h"
#include "StoredScreen.h"

#include "Multiplexer/DaemonClient.h"
#include "Multiplexer/PaneViewing.h"
#include "Multiplexer/RemoteHosts.h"

#include <eacp/Core/Threads/Timer.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
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

// dv for the pane on screen, or the sentence that says why it cannot start.
struct DiffViewOpening
{
    std::optional<DiffViewPane> pane;
    std::string whyNot;
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
    using Clock = std::function<Milliseconds()>;

    // makeLinkToUse dials the host (makeEmbeddedSshLinkFactory on the phone);
    // readClock is a steady millisecond clock. Tests pass their own of both.
    PhoneConnection(PhoneSettings settingsToUse, BridgeLinkFactory makeLinkToUse, Clock readClockToUse);
    ~PhoneConnection();

    // The shell for the pane view (TerminalConfig::makeShell): the daemon's
    // pane, mirrored so its screen can be stored.
    std::unique_ptr<Shell> makeShell(const std::string& paneId);

    const std::string& getHost() const { return host; }
    PhoneLinkState getLinkState() const { return linkState; }
    std::string getFailureReason() const;
    const std::optional<ChosenPane>& getChosenPane() const { return chosenPane; }

    // The periodic check a timer runs every half second: stalls, redials, the
    // last-input wait and storing the screen.
    void handleTick();

    // Moves to another pane of the chosen pane's session (the dots, a swipe
    // along the bar); false for a pane not in that session. The caller then
    // shows it through makeShell.
    bool choosePaneOfSession(const std::string& paneId);

    // The overview's rows from every host in the settings.
    PhoneOverview& getOverview() { return overview; }

    // Makes the row's session the chosen one, on the pane typed into last,
    // else its first, and only one the host's daemon lists. The pane to show
    // through makeShell, or empty when the daemon lists none of its panes.
    std::optional<std::string> openSession(const overview::OverviewRow& row);

    // A tap on a project row: a new session for it, one login shell in its
    // folder, named as the Mac names it, chosen and shown like a session's
    // pane. The pane to show through makeShell, or empty for a row that is
    // not a project its host's last scan listed. Nothing else on the phone
    // starts a shell but dv.
    std::optional<std::string> openProject(const overview::OverviewRow& row);

    // The pane the phone showed last time (the stored screen's), opened at
    // launch when no daemon says which pane was typed into last.
    void setPaneShownLastTime(const PaneOnHostId& pane);

    // dv for the pane on screen, in its folder, as a new temporary pane. Its
    // record is on disk before the daemon is asked anything.
    DiffViewOpening openDiffView();

    // The dv pane's shell (TerminalConfig::makeCommandShell).
    std::unique_ptr<Shell> makeDiffViewShell(const DiffViewPane& pane);

    // dv quit, or the phone left it: the pane is killed, which does nothing
    // once dv has ended, and its record forgotten.
    void closeDiffView(const DiffViewPane& pane);

    // The app went to the background: store the shown screen now, and give a
    // protocol 4 daemon back the size of the pane the phone holds.
    void handleBackground();

    // The app is back, or the network path changed: check the link at once
    // instead of waiting for it to time out, and claim again any size given back.
    void handleWake();

    // The pane screen came up or went away (the overview or dv's screen took
    // its place). While it is up with the app in the foreground, the phone
    // tells the chosen pane's daemon that Jamie is viewing that pane.
    void setPaneScreenShown(bool isShown);

    // What the phone told the daemons it is viewing now.
    const std::optional<ViewedPane>& getViewedPane() const { return viewing.getViewedPane(); }

    std::function<void(PhoneLinkState)> onLinkStateChanged = [](PhoneLinkState) {};
    std::function<void(const ChosenPane&)> onPaneChosen = [](const ChosenPane&) {};

    // A note the pane shows over its text: the link was lost, or another
    // device holds the pane's size ("on Mac, 150 columns; ...").
    std::function<void(const std::string&)> onPaneStatus = [](const std::string&) {};

    // A host's link came up, dropped or failed; reason says why when it failed.
    std::function<void(const std::string& host,
                       RemoteHost::Status status,
                       const std::string& reason)>
        onHostStatusChanged =
            [](const std::string&, RemoteHost::Status, const std::string&) {};

    // The shown pane's first screen arrived from its daemon, at size; borrowed is
    // the size the pane had before the phone took it, when its daemon said.
    std::function<void(
        const ChosenPane& pane, PtySize size, std::optional<PtySize> borrowed)>
        onPaneDrawn = [](const ChosenPane&, PtySize, std::optional<PtySize>) {};

    // Sizes given back to a protocol 4 daemon (background, a stalled link, or
    // at launch, for a pane an earlier run was killed holding).
    std::function<void(const std::string& host,
                       const std::vector<DaemonClient::ReturnedSize>&)>
        onSizesReturned = [](const std::string&,
                             const std::vector<DaemonClient::ReturnedSize>&) {};

    // A dv pane an earlier run started was killed once its host answered.
    std::function<void(const TemporaryPane&)> onEarlierTemporaryPaneKilled = [](const TemporaryPane&) {};

    // Output from the shown pane after a second or more of quiet: how soon
    // what is typed on the Mac shows on the phone.
    std::function<void(const ChosenPane& pane, std::size_t bytes)> onPaneOutputAfterQuiet =
        [](const ChosenPane&, std::size_t) {};

private:
    std::unique_ptr<BridgeLink> makeWatchedLink(const std::string& requestedHost);
    void connectOverviewHost(RemoteHost& remote);
    void handleHostChanged(const std::string& changedHost);
    void reportHostStatus(const std::string& changedHost);
    void handleUnhandledMessage(const std::string& fromHost,
                                const std::string& verb,
                                const std::string& body);
    void redialHosts(Milliseconds now);
    bool shouldRedial(const std::string& overviewHost, Milliseconds now) const;
    const std::string& findShownHost() const;
    RemoteHost& getShownRemoteHost() const;
    void askForLastInput(const std::string& askedHost);
    bool isReadyToChooseLaunchPane() const;
    std::optional<PaneOnHostId> findLaunchPane() const;
    bool isPaneListed(const PaneOnHostId& pane) const;
    void choosePaneWhenKnown();
    void updateChosenPanesSession();
    std::vector<ListedPane> listPanes(const std::string& onHost) const;
    void storeShownScreenWhileLive(Milliseconds now);
    void storeShownScreen();
    bool isShownPaneDrawnButNotReported() const;
    void reportShownPaneDrawn();
    void noteShownPaneOutput(std::size_t bytes);
    void returnBorrowedSizes(const std::string& onHost);
    void claimReturnedSizesAgain();
    void rememberHeldPane();
    void returnSizeHeldByEarlierRun(const std::string& fromHost, const std::string& daemonHello);
    void killTemporaryPanesOfEarlierRuns(const std::string& fromHost);
    void publishSessionLayout(const ProjectSession& session);
    void updateLinkState();
    void updateViewedPane();
    void sendToHost(const std::string& toHost, const std::string& message);

    PhoneSettings settings;
    BridgeLinkFactory makeLink;
    Clock readClock;
    std::string host;
    std::vector<std::string> overviewHosts;
    PhoneOverview overview;
    std::map<std::string, LinkWatch> watches;
    PhoneLinkState linkState = PhoneLinkState::Connecting;
    std::shared_ptr<PaneMirror> mirror = std::make_shared<PaneMirror>();
    std::optional<ChosenPane> chosenPane;
    std::map<std::string, RemoteHost::Status> reportedHostStatus;
    std::set<std::string> hostsAskedForLastInput;
    std::set<std::string> hostsThatAnsweredLastInput;
    std::vector<LastInput> lastInputs;
    std::optional<PaneOnHostId> paneShownLastTime;
    std::filesystem::path heldPanePath;
    std::optional<HeldPane> heldByEarlierRun;
    std::filesystem::path temporaryPanesPath;
    std::vector<TemporaryPane> temporaryPanesOfEarlierRuns;
    std::map<std::string, ProjectSession> projectSessionsStarting;
    std::optional<Milliseconds> firstAskedForLastInputAt;
    Milliseconds startedAt = 0;
    std::string shownPaneId;
    bool hasReportedShownPaneDrawn = false;
    std::optional<Milliseconds> lastShownPaneOutputAt;
    Milliseconds lastStoredAt = 0;
    bool isPaneScreenShown = false;
    bool isInForeground = true;
    PaneViewingReporter viewing {[this](const std::string& toHost, const std::string& message)
                                 { sendToHost(toHost, message); }};
    std::unique_ptr<eacp::Threads::Timer> tickTimer;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

Milliseconds getMillisecondsSinceEpoch();
Milliseconds readSteadyClock();

// The phone's link to each host: EmbeddedSshLink to the settings' destination
// with that "user@host", running their bridge command.
BridgeLinkFactory makeEmbeddedSshLinkFactory(const PhoneSettings& settings);
} // namespace term::phone
