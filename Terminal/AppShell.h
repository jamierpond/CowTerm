#pragma once

#include "ClaudeHud.h"
#include "Config.h"
#include "NotifyUserServer.h"
#include "Palette.h"
#include "PermissionsPanel.h"
#include "Popup.h"
#include "PrDashboard.h"
#include "RudderPublisher.h"
#include "Session.h"
#include "Switcher.h"
#include "TrayController.h"

#include <eacp/UI/UI.h>

#include <functional>
#include <span>
#include <string_view>

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

    // CowTerm > Permissions… (and Reset all permissions…, which opens it on
    // the confirmation). Only macOS has grants to show, so only its menus
    // offer it.
    void showPermissions(bool confirmReset = false);

    void resized() override;

private:
    struct KeyAction
    {
        std::string_view key;
        void (AppShell::*action)();
    };

    bool interceptKey(const eacp::Graphics::KeyEvent& event);
    bool handlePrefixed(const eacp::Graphics::KeyEvent& event);
    bool handleCommand(const eacp::Graphics::KeyEvent& event);
    bool popupKey(const eacp::Graphics::KeyEvent& event);
    static bool isLeaderPrefixKey(const eacp::Graphics::KeyEvent& event);
    static bool isSwitcherKey(const eacp::Graphics::KeyEvent& event);
    bool doesKeyTogglePopup(const std::string& characters) const;
    bool runKeyAction(std::span<const KeyAction> actions, const std::string& characters);
    bool runConfigBinding(const std::string& characters);
    bool runArrowKey(const eacp::Graphics::KeyEvent& event);
    static float calculateArrowResizeCells(bool altHeld);
    bool runPaneDirectionKey(const std::string& characters);
    bool runSessionDigitKey(const std::string& characters);
    void withActivePaneTree(const std::function<void(SessionView&)>& action);
    void withActivePane(const std::function<void(TerminalView&)>& action);
    void showLazygitPopup();
    void splitActivePaneBelow();
    void splitActivePaneBeside();
    void toggleActivePaneZoom();
    void cycleActivePaneFocus();
    void closeActivePane();
    void enterCopyModeOnActivePane();
    void pasteIntoActivePane();
    void openSessionInActiveDirectory();
    void switchToLastSession();
    void quitFromLeader();
    void increaseGlobalFontSize();
    void decreaseGlobalFontSize();
    void resetGlobalFontSize();
    // The overlays are UI components, not native views: one ComponentHost (a
    // GPUView) carries whichever is open, which is why they raise and lower as
    // a single subview rather than one apiece.
    void showOverlay(eacp::UI::Component& overlay);
    void hideOverlay();
    void hideOverlayAndRefocus();
    void focusAttachedPane();
    bool isSwitcherBlocked() const;
    bool isSessionInFrontOfUser(TermSession& session);
    void wireSessionManager();
    void wireTray();
    void wireOverlays();
    void wireWorktreeActions();
    void wireNotifications();
    void raiseOverlay();
    void showPalette();
    void showSwitcher(bool reverse);
    void showPopup(const std::string& command);
    void hidePopup();
    void showPrDashboard();
    void showClaudeHud();
    bool anyOverlayShown() const;
    void attachActive(TermSession& session);
    void setGlobalFontSize(float size);
    void updateTitle();
    void handleSessionNotify(TermSession& session, const std::string& text);
    void rudderChanged();
    void showPaneAndBringToFront(const std::string& shellId);
    std::optional<NotifyUserPaneLabel> describePaneForNotifyUser(const std::string& shellId) const;
    void ringBackForNotifyUser(const NotifyUserRequest& request, const std::string& title);

    AppConfig config = loadConfig();
    // Ahead of the manager, so it outlives the session teardown that still
    // reports pane changes. It only holds the reference until then.
    RudderPublisher rudder {config, manager};
    SessionManager manager {config};
    Palette palette {config, manager};
    Switcher switcher {config, manager};
    PrDashboard prDashboard {config, manager};
    ClaudeHud claudeHud {config, manager};
    Popup popup {config};
    PermissionsPanel permissions {config};
    eacp::UI::ComponentHost overlayHost;
    TrayController tray {manager};
    NotifyUserServer notifyUserServer {config};
    TermSession* attached = nullptr;
    bool prefixArmed = false;
    bool popupPrefixArmed = false;
    bool windowFocused = true;
};
} // namespace term
