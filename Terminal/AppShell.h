#pragma once

#include "ClaudeHud.h"
#include "Config.h"
#include "Palette.h"
#include "Popup.h"
#include "PrDashboard.h"
#include "RemoteHud.h"
#include "Session.h"
#include "SessionCommand.h"
#include "Switcher.h"
#include "TrayController.h"
#include "Web/WebGateway.h"

namespace term
{
// Root content view: hosts the active session's terminal, the palette
// overlay, the Ctrl+A leader-key table and notification routing.
class AppShell final : public eacp::Graphics::View
{
public:
    AppShell();

    // Call once the window is up: restores the saved workspace (or creates
    // the first session) and focuses it.
    void start();

    // Wire to WindowEvents::onActivationChanged: notifications for the
    // active session are suppressed only while the user is actually looking
    // at it, which needs the window's key state.
    void setWindowFocused(bool focused) { windowFocused = focused; }

    std::function<void(const std::string&)> onWindowTitleChanged =
        [](const std::string&) {};
    eacp::Callback onBringToFront = [] {};

    void resized() override;

private:
    bool interceptKey(const eacp::Graphics::KeyEvent& event);
    bool handlePrefixed(const eacp::Graphics::KeyEvent& event);
    bool handleCommand(const eacp::Graphics::KeyEvent& event);
    bool popupKey(const eacp::Graphics::KeyEvent& event);
    void showPalette();
    void hidePalette();
    void showSwitcher(bool reverse);
    void hideSwitcher();
    void showPopup(const std::string& command);
    void hidePopup();
    void showPrDashboard();
    void hidePrDashboard();
    void showClaudeHud();
    void hideClaudeHud();
    void showRemoteHud();
    void hideRemoteHud();

    // Every leader-table action that changes a session funnels through
    // here, which is what makes a mirror behave like a local session:
    // tree-shape commands travel to the CowTerm that owns the tree, view
    // commands (focus, zoom) stay here, where this viewer's answer lives.
    void runSessionCommand(SessionCommand command, float cells = 1.0f);

    // Ctrl+A i and config popup bindings. On a mirror the command runs on
    // the machine that owns the directory and is displayed here.
    void runPopupCommand(const std::string& command);

    // Ctrl+A c. On a mirror the new session is created over there, then
    // mirrored here — the same "new session in this directory" gesture.
    void newSessionHere();

    // Ctrl+A D: stop viewing a mirror without touching the remote (a
    // mirror has no last-pane-closes exit of its own).
    void detachMirror();
    bool anyOverlayShown() const;
    void attachActive(TermSession& session);
    void setGlobalFontSize(float size);
    void updateTitle();
    void handleSessionNotify(TermSession& session, const std::string& text);

    AppConfig config = loadConfig();

    // The fleet's gateway links outlive every session that borrows them:
    // a remote session's panes are RemoteShells pointing into these
    // clients, so this must be declared before the SessionManager and
    // destroyed after it.
    web::RemoteFleet fleet {config.remotes};

    SessionManager manager {config};
    Palette palette {config, manager};
    Switcher switcher {config, manager};
    PrDashboard prDashboard {config, manager};
    ClaudeHud claudeHud {config, manager};

    // The gateway precedes the HUD, which reads its serving state.
    web::WebGateway web {config, manager};
    RemoteHud remoteHud {config, fleet, web};
    Popup popup {config};
    TrayController tray {manager};
    // A session this shell asked a remote to create, waiting for that
    // remote's roster to catch up so it can be mirrored.
    struct PendingMirror
    {
        web::GatewayClient* client = nullptr;
        std::string key;
    };

    PendingMirror pendingMirror;

    TermSession* attached = nullptr;
    bool prefixArmed = false;
    bool popupPrefixArmed = false;
    bool windowFocused = true;
};
} // namespace term
