#include "AppShell.h"

#include "Emulator/Debug.h"
#include <PondVersion.h>
#include "Multiplexer/DaemonClient.h"
#include "Multiplexer/MruStore.h"
#include "Multiplexer/RemoteHosts.h"
#include "Multiplexer/SshBridgeLink.h"
#include "GitHubPageOpener.h"
#include "LeaderKeys.h"
#include "Notifier.h"
#include "Multiplexer/TextLayout.h"
#include "Multiplexer/UsageLog.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Threads/Async.h>
#include <eacp/Core/Threads/EventLoop.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace term
{
using namespace eacp;
using Graphics::KeyEvent;
namespace KeyCode = Graphics::KeyCode;

namespace
{
// How often the Mac checks whether Jamie has gone idle at the focused pane.
constexpr auto viewingCheckInterval = Time::MS {1000};
constexpr auto activityVerb = "activity";
} // namespace

AppShell::AppShell()
{
    windowChromeHost.setRootComponent(windowChrome);
    addSubview(windowChromeHost);

    wireSessionManager();
    wireTray();
    wireOverlays();
    wireNotifications();
}

void AppShell::wireSessionManager()
{
    manager.onActiveChanged = [this](TermSession& session)
    {
        attachActive(session);
        updateTitle();
        rudderChanged();
        updateViewedPane();
    };

    manager.onSessionsChanged = [this]
    {
        closePopupsOfClosedSessions();
        rudderChanged();
    };

    manager.onPanesChanged = [this]
    {
        rudderChanged();
        updateViewedPane();
    };

    manager.onPaneWired = [this](TermSession& session, TerminalView& pane)
    {
        auto* raw = &session;

        pane.interceptKey = [this](const KeyEvent& event)
        { return interceptKey(event); };

        pane.confirmUnsafePaste = [this](const std::string& text, const Callback& paste)
        { pasteConfirmation.ask(text, paste); };

        pane.onTitleChanged = [this, raw](const std::string&)
        {
            if (manager.active() == raw)
                updateTitle();

            rudderChanged();
        };

        rudderChanged();
    };

    manager.onAllClosed = []
    {
        noteQuit("last session closed");
        logUsage("app.quit", {{"via", "last-session-closed"}});
        Apps::quit();
    };

    manager.onNotify = [this](TermSession& session, const std::string& text)
    {
        handleSessionNotify(session, text);
    };
}

void AppShell::wireTray()
{
    tray.onShowWindow = [this]
    {
        logUsage("window.show", {{"via", "tray"}});
        onBringToFront();
    };

    if constexpr (Platform::isMac())
    {
        tray.onShowPermissions = [this]
        {
            onBringToFront();
            showPermissions();
        };
        tray.refresh();
    }
}

void AppShell::wireOverlays()
{
    palette.onClosed = [this] { hideOverlayAndRefocus(); };

    permissions.onClosed = [this] { hideOverlayAndRefocus(); };
    permissions.onRelaunch = [] { permissions::relaunchCowTerm(); };

}

void AppShell::wireNotifications()
{
    // Ring-backs name a session by its key; `ct notify-user` names a pane by
    // its Rudder context id.
    Notifier::initialize(
        [this](const std::string& sessionKey)
        {
            const auto target = parseRudderContextId(sessionKey);

            if (target && target->kind == RudderTarget::Kind::Pane)
            {
                showPaneAndBringToFront(target->value);
                return;
            }

            if (auto* session = manager.find(sessionKey))
            {
                manager.switchTo(*session);
                onBringToFront();
            }
        });

    notifyUserServer.describePane = [this](const std::string& shellId)
    { return describePaneForNotifyUser(shellId); };

    notifyUserServer.onShowPane = [this](const std::string& shellId)
    { showPaneAndBringToFront(shellId); };

    notifyUserServer.onPromptOpened =
        [this](const NotifyUserRequest& request, const std::string& title)
    { ringBackForNotifyUser(request, title); };
}

void AppShell::showPaneAndBringToFront(const std::string& shellId)
{
    if (manager.showPane(shellId))
        onBringToFront();
}

std::optional<NotifyUserPaneLabel>
    AppShell::describePaneForNotifyUser(const std::string& shellId) const
{
    const auto context = rudder.findPaneContext(shellId);

    if (!context)
        return std::nullopt;

    return NotifyUserPaneLabel {context->title, context->subtitle};
}

// The prompt window floats over everything, but while CowTerm is not the app
// in front a desktop notification is what reaches the user.
void AppShell::ringBackForNotifyUser(const NotifyUserRequest& request,
                                     const std::string& title) const
{
    if (windowFocused)
        return;

    Notifier::notify(paneContextId(request.paneId), title, request.content);
}

void AppShell::start()
{
    rudder.onBringToFront = [this] { onBringToFront(); };

    // A pane's running command shows up only in the daemon's info refresh.
    if (auto* daemon = DaemonClient::get())
    {
        daemon->onInfoChanged = [this] { rudderChanged(); };
        daemon->onOtherMessage = [this](const std::string& verb, const std::string& body)
        { takeDaemonMessage({}, verb, body); };
    }

    tray.refresh();
    RemoteHosts::configure(config, makeSshBridgeLink);
    wireRemoteHosts();
    manager.restoreOrCreateInitial();
    startWatchingWhatIsViewed();
}

AppShell::~AppShell()
{
    *alive = false;
    viewingTimer.reset();
    RemoteHosts::teardown();
}

void AppShell::askDaemonsForUnread()
{
    sendToEveryDaemon(proto::make(activityVerb));
}

void AppShell::sendToEveryDaemon(const std::string& message)
{
    sendToDaemonOf({}, message);

    for (auto* remote: RemoteHosts::all())
        remote->getDaemon().send(message);
}

void AppShell::takeDaemonMessage(const std::string& host, const std::string& verb, const std::string& body)
{
    if (verb == activityVerb)
        takeUnreadReport(host, body);
    else if (verb == OutdatedPhoneNag::clientsVerb)
        outdatedPhoneNag.takeConnectedClients(host, body);
}

void AppShell::takeUnreadReport(const std::string& host, const std::string& body)
{
    activityReports.takeActivityReport(host, proto::parse(body).payload);
    palette.refreshRemoteRows();
}

// The focused pane counts as viewed until a minute passes with no key typed;
// a timer notices the minute running out.
void AppShell::startWatchingWhatIsViewed()
{
    noteUserInput();
    viewingTimer = std::make_unique<Threads::Timer>(
        [this, guard = std::weak_ptr<bool> {alive}]
        {
            Threads::callAsync([this, guard]
                               {
                                   if (!guard.expired())
                                       updateViewedPane();
                               });
        },
        viewingCheckInterval);
}

void AppShell::noteUserInput()
{
    lastInputMilliseconds = MruStore::nowMs();
    updateViewedPane();
}

void AppShell::updateViewedPane()
{
    paneViewing.setViewedPane(
        chooseDesktopViewedPane({findFocusedPane(), windowFocused, lastInputMilliseconds, MruStore::nowMs()}));
}

std::optional<ViewedPane> AppShell::findFocusedPane()
{
    auto* session = manager.active();

    if (session == nullptr)
        return std::nullopt;

    const auto* pane = session->view.activePane();

    if (pane == nullptr)
        return std::nullopt;

    return ViewedPane {session->host, pane->shellId()};
}

void AppShell::sendToDaemonOf(const std::string& host, const std::string& message)
{
    if (host.empty())
    {
        if (auto* daemon = DaemonClient::get())
            daemon->send(message);

        return;
    }

    if (auto* remote = RemoteHosts::find(host))
        remote->getDaemon().send(message);
}

// Remote sessions other Macs opened show up here too; the picker refreshes
// as each host's project scan comes back.
void AppShell::wireRemoteHosts()
{
    for (auto* remote: RemoteHosts::all())
    {
        remote->onChanged = [this, remote]
        {
            palette.refreshRemoteRows();

            if (remote->getStatus() == RemoteHost::Status::Connected)
                paneViewing.sendViewingAgainTo(remote->getHost());
        };

        remote->onUnhandledMessage = [this](RemoteHost& from, const std::string& verb, const std::string& body)
        { takeDaemonMessage(from.getHost(), verb, body); };

        remote->onLayout = [this](RemoteHost&, const std::string&, const std::string& layout)
        { manager.adoptRemoteSession(layout); };

        remote->onNotifyUserRequest = [this](RemoteHost& host, const std::string& id, const std::string& body)
        { showRemoteNotifyUserPrompt(host, id, body); };

        remote->onNotifyUserCancelled = [this](RemoteHost& host, const std::string& id)
        { notifyUserServer.closePromptFor(findRemoteAskerIdentity(host, id)); };
    }
}

// A remote request's identity: one token per host and request id, kept until
// the app quits (a handful of small strings per prompt ever shown).
const void* AppShell::findRemoteAskerIdentity(RemoteHost& host, const std::string& id)
{
    return &*remoteAskerIdentities.insert(host.getHost() + "/" + id).first;
}

void AppShell::showRemoteNotifyUserPrompt(RemoteHost& host, const std::string& id, const std::string& body)
{
    const auto request = decodeNotifyUserRequest(body);

    if (!request)
        return;

    auto asker = NotifyUserServer::Asker {
        findRemoteAskerIdentity(host, id),
        [&host, id](const std::string& reply) { host.sendNotifyUserReply(id, reply); }};

    notifyUserServer.showPromptForRequest(asker, *request);
}

void AppShell::rudderChanged()
{
    rudder.schedulePush();
}

void AppShell::attachActive(TermSession& session)
{
    if (attached != &session)
    {
        if (attached != nullptr)
        {
            detachPopupOf(attached);
            attached->view.setVisible(false);
            removeSubview(attached->view);
        }

        attached = &session;
        session.view.setVisible(true);
        addSubview(session.view);
        session.view.repaintAllPanes();

        if (isAttachedPopupShown())
            raisePopup();

        // A live peek swaps the attached terminal while an overlay is open;
        // re-raise the overlay host so it stays on top and keeps keyboard focus.
        raiseOverlay();
    }

    session.view.setBounds(calculateSessionArea());
    windowChrome.showSession(session.host, session.name);

    if (!anyOverlayShown())
        session.view.focusActive();
}

bool AppShell::anyOverlayShown() const
{
    return palette.isShown() || isAttachedPopupShown() || permissions.isShown();
}

void AppShell::setWindowFocused(bool focused)
{
    logUsage(focused ? "window.focus" : "window.blur");
    windowFocused = focused;

    if (focused)
        noteUserInput();
    else
        updateViewedPane();

    for (const auto& session: manager.all())
        for (auto* pane: session->view.panes())
            pane->setWindowFocused(focused);
}

void AppShell::resized()
{
    windowChromeHost.setBounds(calculateWindowChromeBounds(getLocalBounds()));
    const auto bounds = calculateSessionArea();

    if (attached != nullptr)
        attached->view.setBounds(bounds);

    overlayHost.setBounds(bounds);

    if (auto* popup = findAttachedPopup())
        popup->setBounds(bounds);
}

Graphics::Rect AppShell::calculateSessionArea() const
{
    return calculateAreaUnderWindowChrome(getLocalBounds());
}

void AppShell::setGlobalFontSize(float size)
{
    const auto clamped = std::clamp(size, minFontSize, maxFontSize);

    if (clamped == config.fontSize)
        return;

    config.fontSize = clamped;

    for (const auto& session: manager.all())
        session->view.setFontSize(clamped);

    saveConfig(config);
}

void AppShell::updateTitle()
{
    auto* session = manager.active();

    if (session == nullptr)
        return;

    auto title = session->name;

    if (!session->activeTitle().empty())
        title += " — " + session->activeTitle();

    // Trailing build tag so it is always clear which commit is running.
    title += "  ·  " + pond::getVersionString();

    onWindowTitleChanged(title);
}

bool AppShell::isSessionInFrontOfUser(TermSession& session)
{
    const auto& paneTree = std::as_const(session.view);
    const auto* pane = paneTree.activePane();

    if (!windowFocused || manager.active() != &session)
        return false;

    return pane != nullptr && pane->hasFocus();
}

void AppShell::handleSessionNotify(TermSession& session, const std::string& text)
{
    // The active, focused session is right in front of the user — no need
    // to shout about it.
    if (isSessionInFrontOfUser(session))
        return;

    Notifier::notify(session.key(), session.name, text);
}

void AppShell::showOverlay(eacp::UI::Component& overlay)
{
    // setRootComponent sizes the component to the host, so the host is what
    // gets the bounds; the overlay follows it.
    overlayHost.setRootComponent(overlay);
    removeSubview(overlayHost);
    addSubview(overlayHost);
    overlayHost.setBounds(calculateSessionArea());
    overlay.grabKeyboardFocus();

    // Taking the host out of the view tree above dropped it as the window's
    // first responder, and grabKeyboardFocus() does nothing when the same
    // component is already focused -- so a re-raise (a peek swapping the
    // terminal under an open overlay) would leave the overlay drawn but deaf.
    // Ask for the keyboard at the native level too.
    overlayHost.focus();
}

void AppShell::hideOverlay()
{
    removeSubview(overlayHost);
}

void AppShell::hideOverlayAndRefocus()
{
    hideOverlay();
    focusAttachedPane();
}

void AppShell::focusAttachedPane()
{
    if (attached != nullptr)
        attached->view.focusActive();
}

// Re-raise whichever overlay is open above a freshly attached terminal view.
void AppShell::raiseOverlay()
{
    if (palette.isShown())
        showOverlay(palette);
    else if (permissions.isShown())
        showOverlay(permissions);

    if (isAttachedPopupShown())
        raisePopup();
}

// The popup is a sibling of the session view, not an overlay; a session
// attached under it (a notification click, the active session exiting) lands
// on top and would hide lazygit while it keeps running, and Ctrl+A i then
// does nothing because the popup counts as shown.
void AppShell::raisePopup()
{
    auto* popup = findAttachedPopup();

    if (popup == nullptr)
        return;

    removeSubview(*popup);
    popup->setVisible(true);
    addSubview(*popup);
    popup->setBounds(calculateSessionArea());
    popup->focusTerminal();
}

Popup* AppShell::findAttachedPopup() const
{
    if (auto found = popupsBySession.find(attached); found != popupsBySession.end())
        return found->second.get();

    return nullptr;
}

bool AppShell::isAttachedPopupShown() const
{
    const auto* popup = findAttachedPopup();
    return popup != nullptr && popup->isShown();
}

// A session switched away from keeps its popup running, just off screen and
// hidden, which frees the GPU memory its frames are drawn in.
void AppShell::detachPopupOf(const TermSession* session)
{
    if (auto found = popupsBySession.find(session); found != popupsBySession.end())
    {
        found->second->setVisible(false);
        removeSubview(*found->second);
    }
}

Popup& AppShell::makePopupFor(const TermSession& session)
{
    auto& popup = *(popupsBySession[&session] = std::make_unique<Popup>(config));
    const auto* owner = &session;

    popup.onClosed = [this, owner]
    {
        logUsage("popup.close");
        hidePopup(*owner);
    };

    popup.interceptKey = [this](const KeyEvent& event) { return popupKey(event); };
    return popup;
}

// Closing a session takes its popup (and the program in it) with it.
void AppShell::closePopupsOfClosedSessions()
{
    for (const auto& [session, popup]: popupsBySession)
    {
        const auto isOpen = std::ranges::any_of(
            manager.all(), [session](const auto& open) { return open.get() == session; });

        if (!isOpen)
            popup->dismiss();
    }
}

void AppShell::showPalette()
{
    if (palette.isShown())
        return;

    showOverlay(palette);
    palette.show();
    askDaemonsForUnread();
}

// Like the other overlays, it waits for whichever one is open to close.
void AppShell::showPermissions(bool confirmReset)
{
    if (anyOverlayShown() && !permissions.isShown())
        return;

    showOverlay(permissions);
    permissions.show(confirmReset);
}

void AppShell::showPopup(const std::string& command)
{
    auto* active = manager.active();

    if (active == nullptr || isAttachedPopupShown())
        return;

    auto& popup = makePopupFor(*active);
    popupPrefixArmed = false;
    logUsage("popup.open", {{"command", command}, {"host", active->host}});
    addSubview(popup);
    popup.setBounds(calculateSessionArea());
    popup.show(command, active->activeWorkingDirectory(), active->host);
}

// The popup goes on the next tick: this runs inside its own close callback.
void AppShell::hidePopup(const TermSession& session)
{
    detachPopupOf(&session);

    Threads::callAsync([this, owner = &session] { popupsBySession.erase(owner); });

    if (attached == &session)
        focusAttachedPane();
}

bool AppShell::isLeaderPrefixKey(const KeyEvent& event)
{
    return event.modifiers.control && event.charactersIgnoringModifiers == "a"
           && !event.modifiers.command;
}

bool AppShell::doesKeyTogglePopup(const std::string& characters) const
{
    return characters == "i";
}

bool AppShell::popupKey(const KeyEvent& event)
{
    if (!popupPrefixArmed)
    {
        popupPrefixArmed = isLeaderPrefixKey(event);
        return popupPrefixArmed;
    }

    popupPrefixArmed = false;

    if (event.modifiers.control && event.charactersIgnoringModifiers == "a")
        return false;

    // The rest of the leader table targets panes hidden behind the
    // popup; swallow rather than mutate what the user can't see.
    if (auto* popup = findAttachedPopup(); popup != nullptr && doesKeyTogglePopup(event.charactersIgnoringModifiers))
        popup->dismiss();

    return true;
}

bool AppShell::interceptKey(const KeyEvent& event)
{
    noteUserInput();

    if (prefixArmed)
    {
        prefixArmed = false;
        return handlePrefixed(event);
    }

    if (isLeaderPrefixKey(event))
    {
        prefixArmed = true;
        return true;
    }

    if (event.modifiers.command)
        return handleCommand(event);

    return false;
}

void AppShell::withActivePaneTree(const std::function<void(SessionView&)>& action)
{
    if (auto* active = manager.active())
        action(active->view);
}

void AppShell::withActivePane(const std::function<void(TerminalView&)>& action)
{
    withActivePaneTree(
        [&](SessionView& paneTree)
        {
            if (auto* pane = paneTree.activePane())
                action(*pane);
        });
}

bool AppShell::runKeyAction(std::span<const KeyAction> actions,
                            const std::string& characters)
{
    const auto found = std::ranges::find(actions, characters, &KeyAction::key);

    if (found == actions.end())
        return false;

    (this->*found->action)();
    return true;
}

// Arrows move focus (tmux select-pane) whether or not Ctrl is still down
// from the prefix roll.
bool AppShell::runArrowKey(const KeyEvent& event)
{
    using ArrowDirection = std::pair<std::uint16_t, char>;

    constexpr auto arrowDirections = std::array {
        ArrowDirection {KeyCode::LeftArrow, 'h'},
        ArrowDirection {KeyCode::DownArrow, 'j'},
        ArrowDirection {KeyCode::UpArrow, 'k'},
        ArrowDirection {KeyCode::RightArrow, 'l'},
    };

    const auto found =
        std::ranges::find(arrowDirections, event.keyCode, &ArrowDirection::first);

    if (found == arrowDirections.end())
        return false;

    const auto direction = found->second;
    withActivePaneTree([&](SessionView& paneTree) { paneTree.focusDirection(direction); });
    return true;
}

// h/j/k/l move focus whether or not Ctrl is still down from the prefix roll.
bool AppShell::runPaneDirectionKey(const std::string& characters)
{
    if (const auto direction = findPaneFocusDirection(characters))
    {
        withActivePaneTree([&](SessionView& paneTree)
                           { paneTree.focusDirection(*direction); });
        return true;
    }

    return false;
}

bool AppShell::runSessionDigitKey(const std::string& characters)
{
    const auto index = findSessionIndexForDigit(characters);

    if (!index)
        return false;

    manager.switchToIndex(*index);
    return true;
}

void AppShell::logCommandKey(std::string_view event, const KeyEvent& key)
{
    const auto& characters = key.charactersIgnoringModifiers;

    if (isPrintableTypedText(characters))
        logUsage(event, {{"key", characters}});
    else
        logUsage(event, {{"code", key.keyCode}});
}

bool AppShell::handlePrefixed(const KeyEvent& event)
{
    logCommandKey("leader.key", event);
    return runPrefixedKey(event);
}

bool AppShell::runPrefixedKey(const KeyEvent& event)
{
    // f palette, i lazygit popup, " and %
    // split below and beside, [ ] copy mode and paste, x kills the pane,
    // g and r open the pane's repository and its branch's pull request on GitHub.
    static constexpr auto leaderActions = std::array {
        KeyAction {"f", &AppShell::showPalette},
        KeyAction {"i", &AppShell::showLazygitPopup},
        KeyAction {"\"", &AppShell::splitActivePaneBelow},
        KeyAction {"%", &AppShell::splitActivePaneBeside},
        KeyAction {"z", &AppShell::toggleActivePaneZoom},
        KeyAction {"[", &AppShell::enterCopyModeOnActivePane},
        KeyAction {"]", &AppShell::pasteIntoActivePane},
        KeyAction {"o", &AppShell::cycleActivePaneFocus},
        KeyAction {"x", &AppShell::closeActivePane},
        KeyAction {"g", &AppShell::openGitHubRepositoryOfActivePane},
        KeyAction {"r", &AppShell::openGitHubPullRequestOfActivePane},
    };

    const auto& characters = event.charactersIgnoringModifiers;

    // Prefix twice sends a literal Ctrl+A through to the shell.
    if (event.modifiers.control && characters == "a")
        return false;

    if (runArrowKey(event))
        return true;

    if (runPaneDirectionKey(characters) || runKeyAction(leaderActions, characters))
        return true;

    runSessionDigitKey(characters);
    return true;
}

bool AppShell::handleCommand(const KeyEvent& event)
{
    // Zoom (= + -) is one size for the whole app: every pane in every
    // session changes together, written back to ~/.config/cowterm.json.
    static constexpr auto commandActions = std::array {
        KeyAction {"k", &AppShell::showPalette},
        KeyAction {"t", &AppShell::showPalette},
        KeyAction {"n", &AppShell::openSessionInActiveDirectory},
        KeyAction {"q", &AppShell::quitFromLeader},
        KeyAction {"=", &AppShell::increaseGlobalFontSize},
        KeyAction {"+", &AppShell::increaseGlobalFontSize},
        KeyAction {"-", &AppShell::decreaseGlobalFontSize},
    };

    const auto& characters = event.charactersIgnoringModifiers;
    const auto handled =
        runKeyAction(commandActions, characters) || runSessionDigitKey(characters);

    if (handled)
        logCommandKey("command.key", event);

    return handled;
}

// Lazygit over the active pane's directory, tmux display-popup style;
// quitting lazygit (or Ctrl+A i again) dismisses it.
void AppShell::showLazygitPopup()
{
    showPopup("lazygit");
}

void AppShell::splitActivePaneBelow()
{
    withActivePaneTree([](SessionView& paneTree) { paneTree.splitActive(false); });
}

void AppShell::splitActivePaneBeside()
{
    withActivePaneTree([](SessionView& paneTree) { paneTree.splitActive(true); });
}

void AppShell::toggleActivePaneZoom()
{
    withActivePaneTree([](SessionView& paneTree) { paneTree.toggleZoom(); });
}

void AppShell::cycleActivePaneFocus()
{
    withActivePaneTree([](SessionView& paneTree) { paneTree.cycleFocus(); });
}

// The session ends with its last pane.
void AppShell::closeActivePane()
{
    withActivePaneTree([](SessionView& paneTree) { paneTree.closeActivePane(); });
}

void AppShell::enterCopyModeOnActivePane()
{
    withActivePane([](TerminalView& pane) { pane.enterCopyMode(); });
}

void AppShell::pasteIntoActivePane()
{
    withActivePane([](TerminalView& pane) { pane.paste(); });
}

void AppShell::openSessionInActiveDirectory()
{
    if (auto* active = manager.active())
        manager.newSession(active->getActiveDirectoryAsProjectDir());
}

void AppShell::openGitHubRepositoryOfActivePane()
{
    openGitHubPageOfActivePane(github::PageKind::repository);
}

void AppShell::openGitHubPullRequestOfActivePane()
{
    openGitHubPageOfActivePane(github::PageKind::pullRequest);
}

// The page opens in this machine's browser even when the pane's shell runs on
// another host.
void AppShell::openGitHubPageOfActivePane(github::PageKind kind)
{
    auto* active = manager.active();

    if (active == nullptr)
        return;

    github::openPageForFolder({active->host, active->activeWorkingDirectory()},
                              kind,
                              [sessionKey = active->key(), sessionName = active->name](const std::string& reason)
                              { Notifier::notify(sessionKey, sessionName, reason); });
}

void AppShell::quitFromLeader()
{
    noteQuit("leader q");
    logUsage("app.quit", {{"via", "command-q"}});
    Apps::quit();
}

void AppShell::increaseGlobalFontSize()
{
    setGlobalFontSize(config.fontSize + 1);
}

void AppShell::decreaseGlobalFontSize()
{
    setGlobalFontSize(config.fontSize - 1);
}
} // namespace term
