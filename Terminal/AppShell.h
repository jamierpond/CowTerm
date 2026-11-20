#pragma once

#include "Multiplexer/Config.h"
#include "Multiplexer/HostColours.h"
#include "Multiplexer/PaneViewing.h"
#include "Multiplexer/ActivityReports.h"
#include "NotifyUserServer.h"
#include "PasteConfirmation.h"
#include "Palette.h"
#include "CowTermPermissions.h"
#include "Popup.h"
#include "RudderPublisher.h"
#include "Session.h"
#include "Switcher.h"
#include "TrayController.h"
#include "WindowChrome.h"

#include <eacp/Core/Threads/Timer.h>
#include <eacp/UI/UI.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string_view>

namespace term
{
class RemoteHost;

// Root content view: hosts the active session's terminal, the palette
// overlay, the Ctrl+A leader-key table and notification routing.
class AppShell final : public eacp::Graphics::View
{
public:
    AppShell();
    ~AppShell() override;

    // Call once the window is up: restores the saved workspace (or creates
    // the first session) and focuses it.
    void start();

    // Wire to WindowEvents::onActivationChanged: notifications for the
    // active session are suppressed only while the user is actually looking
    // at it, which needs the window's key state.
    void setWindowFocused(bool focused);

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
    bool runPrefixedKey(const eacp::Graphics::KeyEvent& event);
    static void logCommandKey(std::string_view event, const eacp::Graphics::KeyEvent& key);
    bool handleCommand(const eacp::Graphics::KeyEvent& event);
    bool popupKey(const eacp::Graphics::KeyEvent& event);
    static bool isLeaderPrefixKey(const eacp::Graphics::KeyEvent& event);
    static bool isSwitcherKey(const eacp::Graphics::KeyEvent& event);
    bool doesKeyTogglePopup(const std::string& characters) const;
    bool runKeyAction(std::span<const KeyAction> actions, const std::string& characters);
    bool runArrowKey(const eacp::Graphics::KeyEvent& event);
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
    void quitFromLeader();
    void increaseGlobalFontSize();
    void decreaseGlobalFontSize();
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
    void wireRemoteHosts();
    const void* findRemoteAskerIdentity(RemoteHost& host, const std::string& id);
    void showRemoteNotifyUserPrompt(RemoteHost& host, const std::string& id, const std::string& body);
    void wireTray();
    void wireOverlays();
    void wireNotifications();
    void raiseOverlay();
    void raisePopup();
    void showPalette();
    void showSwitcher(bool reverse);
    void showPopup(const std::string& command);
    void hidePopup(const TermSession& session);
    Popup* findAttachedPopup() const;
    bool isAttachedPopupShown() const;
    Popup& makePopupFor(const TermSession& session);
    void detachPopupOf(const TermSession* session);
    void closePopupsOfClosedSessions();
    bool anyOverlayShown() const;
    eacp::Graphics::Rect calculateSessionArea() const;
    void attachActive(TermSession& session);
    void setGlobalFontSize(float size);
    void updateTitle();
    void handleSessionNotify(TermSession& session, const std::string& text);
    void askDaemonsForUnread();
    void takeUnreadReport(const std::string& host, const std::string& body);
    void startWatchingWhatIsViewed();
    void noteUserInput();
    void updateViewedPane();
    std::optional<ViewedPane> findFocusedPane();
    static void sendToDaemonOf(const std::string& host, const std::string& message);
    void rudderChanged();
    void showPaneAndBringToFront(const std::string& shellId);
    std::optional<NotifyUserPaneLabel> describePaneForNotifyUser(const std::string& shellId) const;
    void ringBackForNotifyUser(const NotifyUserRequest& request,
                               const std::string& title) const;

    AppConfig config = loadConfig();
    const HostRowColours thisMachineColours = getThisMachineColours(config);
    // Ahead of the manager, so it outlives the session teardown that still
    // reports pane changes. It only holds the reference until then.
    RudderPublisher rudder {config, manager};
    SessionManager manager {config};

    // What each daemon last said is unread, drawn by the palette and switcher.
    ActivityReports activityReports;
    Palette palette {config, thisMachineColours, manager, activityReports};
    Switcher switcher {config, thisMachineColours, manager, activityReports};

    // Tells the daemons which pane Jamie is looking at (PaneViewing.h).
    PaneViewingReporter paneViewing {&AppShell::sendToDaemonOf};
    std::int64_t lastInputMilliseconds = 0;
    std::unique_ptr<eacp::Threads::Timer> viewingTimer;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    // Each session's own Ctrl+A i popup, kept while the session is switched
    // away from and shown again when it comes back.
    std::map<const TermSession*, std::unique_ptr<Popup>> popupsBySession;
    pond::PermissionsPanel permissions {permissions::makePermissionsPanelSettings(config)};
    eacp::UI::ComponentHost overlayHost;
    WindowChrome windowChrome {config, thisMachineColours};
    eacp::UI::ComponentHost windowChromeHost;
    TrayController tray;
    NotifyUserServer notifyUserServer {config};
    PasteConfirmation pasteConfirmation {config};
    std::set<std::string> remoteAskerIdentities;
    TermSession* attached = nullptr;
    bool prefixArmed = false;
    bool popupPrefixArmed = false;
    bool windowFocused = true;
};
} // namespace term
