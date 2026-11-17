#pragma once

#include "DaemonLink.h"
#include "Protocol.h"

#include "Shell/Shell.h"

#include <eacp/Core/Threads/Timer.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace term
{
// A pane's shell in this machine's daemon, in its session sessionName, or
// in-process when the daemon is unreachable.
std::unique_ptr<Shell> makeLocalShell(const std::string& shellId, const std::string& sessionName);

// A pane's shell in host's daemon, in that daemon's session sessionName.
std::unique_ptr<Shell> makeRemoteShell(const std::string& shellId,
                                       const std::string& host,
                                       const std::string& sessionName);

// The app's end of a session daemon: one conversation carrying every pane.
// Shells are keyed by stable id — spawn adopts a still-running shell
// (sending its serialized terminal state) or starts a fresh one; release
// stops listening without killing, which is how shells outlive the GUI.
//
// A remote daemon's link can drop and come back: its panes stay routed
// meanwhile, typing goes nowhere, and replaceLink re-attaches every pane,
// which redraws it from the daemon's state.
//
// Main-thread object, like the link it rides on.
class DaemonClient
{
public:
    struct SizeOwnerNotice
    {
        PtySize size;
        bool isThisClientTheOwner = true;
        std::string ownerName;
    };

    struct Routes
    {
        std::function<void(std::string)> onOutput = [](const std::string&) {};
        std::function<void(const std::string&)> onState = [](const std::string&) {};
        eacp::Callback onExit = [] {};

        // A remote pane's link dropped; it will be re-attached when it returns.
        std::function<void(const std::string& reason)> onLinkLost = [](const std::string&) {};

        // The pane's command isn't installed on the daemon's machine.
        std::function<void(const std::string& program)> onMissingCommand = [](const std::string&) {};

        // Another client (or this one) now sets the pane's size.
        std::function<void(const SizeOwnerNotice&)> onSizeOwnerChanged = [](const SizeOwnerNotice&) {};

        // The daemon does not list the pane, and this client opens only panes
        // it lists (setOpensOnlyListedPanes): nothing was started.
        eacp::Callback onGone = [] {};
    };

    struct ShellInfo
    {
        std::string foregroundProcess;
        std::string workingDirectory;
        std::string sessionName;

        bool operator==(const ShellInfo&) const = default;
    };

    // A pane given back the size it had when this client adopted it.
    struct ReturnedSize
    {
        std::string paneId;
        PtySize size;
    };

    struct SpawnRequest
    {
        std::string id;
        PtySize size;
        std::string workingDirectory;
        std::string sessionName;

        // Empty for an interactive shell.
        std::string command;
    };

    // Dials the local daemon, launching the bundled one when nobody answers,
    // and calls whenReady once the outcome is known either way. get()
    // answers null until then — and permanently when the daemon is
    // unreachable, which drops every local pane back to in-process shells.
    static void initialize(const eacp::Callback& whenReady);

    // What other clients see this process called ("Mac", "iPhone") when it
    // holds a pane's size. Set once at startup, before any client connects.
    static void setClientName(const std::string& name);

    // The phone takes the size of every pane it opens or resizes, instead of
    // waiting for the Mac to let go. Set once at startup.
    static void setClaimsSizeOfEveryPaneItShows(bool claims);

    // The phone never starts a shell: before each spawn it asks for info, and
    // a pane the daemon does not list gets onGone instead of a spawn. Set once
    // at startup.
    static void setOpensOnlyListedPanes(bool opensOnlyListed);

    // The one command such a client may still start, in a new pane of its
    // own (the phone's dv); a spawn of any other command gets onGone. Empty,
    // the default, allows none. Set once at startup.
    static void setOnlyCommandItMayStart(const std::string& command);
    static DaemonClient* get();
    static void teardown();

    // keepsPanesWhenLinkDrops: a remote daemon's panes wait for the link to
    // come back; the local daemon going away ends them.
    DaemonClient(std::unique_ptr<DaemonLink> linkToUse, bool keepsPanesWhenLinkDrops);

    bool isConnected() const;

    // A fresh link to the same daemon after the old one dropped.
    void replaceLink(std::unique_ptr<DaemonLink> linkToUse);

    // Attaches every routed pane again once a replaced link is up; each
    // redraws from the state the daemon sends back.
    void reattachPanes();

    // Every routed pane hears that the link is down, with the reason.
    void reportLinkLost(const std::string& reason);

    void spawn(const SpawnRequest& request, Routes routes);

    // A pane the phone may start a login shell in, and the only folder it may.
    struct LoginShellPane
    {
        std::string paneId;
        std::string folder;
    };

    // The phone's one way to start a shell, from a tap on a project: the next
    // spawn of the pane is sent though the daemon does not list it, if it asks
    // for the login shell (no command) in exactly that folder. Used once; a
    // spawn of the pane in any other folder gets onGone.
    void allowLoginShellInNewPane(const LoginShellPane& pane);

    void write(const std::string& id, std::string_view data);
    void writeUserInput(const std::string& id, std::string_view data);
    void resize(const std::string& id, const PtySize& size);

    // Sets the pane's size from now on, whoever set it before.
    void claimSize(const std::string& id, const PtySize& size);

    // Takes back every pane another client holds, at this client's sizes.
    void reclaimSizes();

    // When the app leaves the foreground or the link stalls, the panes this
    // client holds go back to whoever held them before. A daemon with size
    // owners does that itself, asked with yield-size. One from before size
    // owners (protocol 4) never does, so a client that claims sizes keeps each
    // pane's size from the first state the daemon sent for it and puts it back
    // by hand, here and on release; only these are listed.
    std::vector<ReturnedSize> returnBorrowedSizes();

    // Claims again, at this client's size, every pane it shows: those
    // returnBorrowedSizes gave back, and those a daemon took while it was quiet.
    void claimReturnedSizesAgain();

    // The size the pane had when this client adopted it; empty when it has none.
    std::optional<PtySize> findBorrowedSize(const std::string& id) const;

    // From the daemon's "daemon-hello"; empty until it answers.
    std::optional<int> getDaemonProtocolVersion() const
    {
        return answeredProtocolVersion;
    }

    bool hasPanesHeldElsewhere() const;
    bool isPaneHeldElsewhere(const std::string& id) const;
    void kill(const std::string& id);
    void release(const std::string& id);

    ShellInfo infoFor(const std::string& id) const;

    // Every shell the last info refresh listed, by id in alphabetical order.
    std::vector<std::string> listShellIds() const;

    // Raw messages that aren't about a pane ("layouts", "scan-result").
    std::function<void(const std::string& verb, const std::string& body)> onOtherMessage =
        [](const std::string&, const std::string&) {};

    void send(const std::string& message);

    // A shell's foreground process or directory changed, as seen by the
    // info refresh that already runs. Not fired when nothing moved.
    eacp::Callback onInfoChanged = [] {};

    // Tears the whole server down — every surviving shell dies. The tray's
    // "kill everything" path.
    void killServer();

private:
    struct Route
    {
        Routes routes;
        SpawnRequest request;
    };

    void wireLink();
    void sendHello();
    void handleLinkDropped();
    void handleSizeOwner(const proto::Message& message);
    void sendSpawn(const SpawnRequest& request);
    void sendSpawnOnceListed(const SpawnRequest& request);
    void openListedPanesOrReportGone();
    void sendCommandSpawnIfAllowed(const SpawnRequest& request);
    void sendLoginShellSpawnIfAllowed(const SpawnRequest& request);
    void reportPaneGone(const std::string& id);
    void sendResizeAndClaim(const std::string& id, const PtySize& size);
    void rememberSizeFoundOnAdopt(const std::string& id,
                                  const std::string& serializedState);
    bool doesDaemonKeepSizeOwners() const;
    bool hasSizeToReturn(const std::string& id) const;
    void yieldClaimedSizes();
    std::optional<ReturnedSize> returnBorrowedSize(const std::string& id);
    void forgetBorrowedSize(const std::string& id);
    void handleMessage(const std::string& body);
    void handleClientMessage(const proto::Message& message, const std::string& body);
    void handlePaneMessage(proto::Message message);
    void handleOutput(const std::string& id, std::string payload);
    void handleState(const std::string& id, const std::string& payload);
    void handleExit(const std::string& id);
    void handleMissingCommand(const std::string& id, const std::string& program);
    void handleInfo(const std::string& payload);
    void readInfoLine(const std::string& line);
    void requestInfo();

    std::unique_ptr<DaemonLink> link;
    bool keepsPanesWhenLinkDrops = false;
    std::unordered_map<std::string, Route> routes;
    std::unordered_map<std::string, ShellInfo> info;
    std::unordered_set<std::string> panesHeldElsewhere;
    std::vector<std::string> spawnsWaitingForInfo;
    std::unordered_map<std::string, std::string> foldersOfLoginShellsAllowedToStart;
    std::unordered_map<std::string, PtySize> borrowedSizes;
    std::unordered_set<std::string> panesWithSizeReturned;
    std::optional<int> answeredProtocolVersion;
    std::unique_ptr<eacp::Threads::Timer> infoTimer;
};
} // namespace term
