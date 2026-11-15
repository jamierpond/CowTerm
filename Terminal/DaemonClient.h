#pragma once

#include "Config.h"
#include "DaemonLink.h"

#include "Shell/Pty.h"
#include "TerminalView/TerminalView.h"

#include <eacp/Core/Threads/Timer.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace term
{
// The widget's slice of the user's config, wired to the daemon of the
// machine the panes live on: this one when host is empty, otherwise the
// remote host's (see RemoteHosts.h). How the app builds every TerminalView.
TerminalConfig terminalConfig(const AppConfig& config,
                              const std::string& host = {},
                              const std::string& sessionName = {});

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
    struct Routes
    {
        std::function<void(std::string)> onOutput = [](const std::string&) {};
        std::function<void(const std::string&)> onState = [](const std::string&) {};
        eacp::Callback onExit = [] {};

        // A remote pane's link dropped; it will be re-attached when it returns.
        std::function<void(const std::string& reason)> onLinkLost = [](const std::string&) {};

        // The pane's command isn't installed on the daemon's machine.
        std::function<void(const std::string& program)> onMissingCommand = [](const std::string&) {};
    };

    struct ShellInfo
    {
        std::string foregroundProcess;
        std::string workingDirectory;
        std::string sessionName;

        bool operator==(const ShellInfo&) const = default;
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
    void write(const std::string& id, std::string_view data);
    void resize(const std::string& id, const PtySize& size);
    void kill(const std::string& id);
    void release(const std::string& id);

    ShellInfo infoFor(const std::string& id) const;

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
    void handleLinkDropped();
    void sendSpawn(const SpawnRequest& request);
    void handleMessage(const std::string& body);
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
    std::unique_ptr<eacp::Threads::Timer> infoTimer;
};
} // namespace term
