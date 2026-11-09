#include "AppShell.h"

#include "CowTermCore/Debug.h"
#include "CowTermVersion.h"
#include "DaemonClient.h"
#include "GitWorktree.h"
#include "LeaderKeys.h"
#include "Notifier.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Threads/EventLoop.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace term
{
using namespace eacp;
using Graphics::KeyEvent;
namespace KeyCode = Graphics::KeyCode;

AppShell::AppShell()
{
    wireSessionManager();
    wireTray();
    wireOverlays();
    wireWorktreeActions();
    wireNotifications();
}

void AppShell::wireSessionManager()
{
    manager.onActiveChanged = [this](TermSession& session)
    {
        attachActive(session);
        updateTitle();
        tray.refresh();
        rudderChanged();
    };

    manager.onSessionsChanged = [this]
    {
        tray.refresh();
        rudderChanged();
    };

    manager.onPanesChanged = [this] { rudderChanged(); };

    manager.onPaneWired = [this](TermSession& session, TerminalView& pane)
    {
        auto* raw = &session;

        pane.interceptKey = [this](const KeyEvent& event)
        { return interceptKey(event); };

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
        Apps::quit();
    };

    manager.onNotify = [this](TermSession& session, const std::string& text)
    {
        handleSessionNotify(session, text);
        tray.refresh();
    };
}

void AppShell::wireTray()
{
    tray.onShowWindow = [this] { onBringToFront(); };

    tray.onPickSession = [this](TermSession& session)
    {
        manager.switchTo(session);
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

    switcher.onClosed = [this] { hideOverlayAndRefocus(); };

    prDashboard.onClosed = [this] { hideOverlayAndRefocus(); };

    claudeHud.onClosed = [this] { hideOverlayAndRefocus(); };

    permissions.onClosed = [this] { hideOverlayAndRefocus(); };

    popup.onClosed = [this] { hidePopup(); };

    popup.interceptKey = [this](const KeyEvent& event) { return popupKey(event); };
}

void AppShell::wireWorktreeActions()
{
    // Branch a highlighted repo into a fresh worktree and drop a session in
    // it. Returns git's error on failure so the palette can show it in place.
    palette.onCreateWorktree =
        [this](const std::string& repoPath, const std::string& branch) -> std::string
    {
        const auto result = createWorktree(repoPath, branch);

        if (!result.ok)
            return result.message;

        manager.newSession(result.path);
        return {};
    };

    // Trash a highlighted worktree: bin the checkout, then close any session
    // still open on it (its directory is gone). Git's error, if any, goes back
    // to the confirm prompt.
    palette.onRemoveWorktree = [this](const std::string& worktreePath) -> std::string
    {
        const auto result = removeWorktree(worktreePath);

        if (!result.ok)
            return result.message;

        if (auto* session = manager.find(worktreePath))
            manager.close(*session);

        return {};
    };
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
                                     const std::string& title)
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
        daemon->onInfoChanged = [this] { rudderChanged(); };

    manager.restoreOrCreateInitial();

    // Unlock first: a required grant missing opens the panel over the
    // restored sessions, and it goes again by itself once granted. Never on
    // Windows or Linux, where every grant is always in force.
    if (PermissionsPanel::requiredMissingNow())
        Threads::callAsync(
            [this]
            {
                if (!anyOverlayShown())
                {
                    showOverlay(permissions);
                    permissions.show(true);
                }
            });
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
            removeSubview(attached->view);

        attached = &session;
        addSubview(session.view);

        // A live peek swaps the attached terminal while an overlay is open;
        // re-raise the overlay host so it stays on top and keeps keyboard focus.
        raiseOverlay();
    }

    session.view.setBounds(getLocalBounds());

    if (!anyOverlayShown())
        session.view.focusActive();
}

bool AppShell::anyOverlayShown() const
{
    return palette.isShown() || switcher.isShown() || prDashboard.isShown()
           || claudeHud.isShown() || popup.isShown() || permissions.isShown();
}

void AppShell::resized()
{
    const auto bounds = getLocalBounds();

    if (attached != nullptr)
        attached->view.setBounds(bounds);

    overlayHost.setBounds(bounds);
    popup.setBounds(bounds);
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
    title += "  ·  " + versionTag();

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
    overlayHost.setBounds(getLocalBounds());
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
    else if (switcher.isShown())
        showOverlay(switcher);
    else if (prDashboard.isShown())
        showOverlay(prDashboard);
    else if (claudeHud.isShown())
        showOverlay(claudeHud);
    else if (permissions.isShown())
        showOverlay(permissions);
}

void AppShell::showPalette()
{
    if (palette.isShown())
        return;

    showOverlay(palette);
    palette.show();
}

void AppShell::showSwitcher(bool reverse)
{
    if (isSwitcherBlocked())
        return;

    showOverlay(switcher);

    // begin() only opens with two or more sessions to flip between; if it
    // declines, take the overlay back down and leave the pane focused.
    if (!switcher.begin(reverse))
    {
        hideOverlay();
        return;
    }

}

bool AppShell::isSwitcherBlocked() const
{
    return switcher.isShown() || palette.isShown() || popup.isShown();
}

void AppShell::showPrDashboard()
{
    if (anyOverlayShown())
        return;

    showOverlay(prDashboard);
    prDashboard.show();
}

void AppShell::showClaudeHud()
{
    if (anyOverlayShown())
        return;

    showOverlay(claudeHud);
    claudeHud.show();
}

// Like the other overlays, it waits for whichever one is open to close.
void AppShell::showPermissions(bool confirmReset)
{
    if (anyOverlayShown() && !permissions.isShown())
        return;

    showOverlay(permissions);
    permissions.show(false, confirmReset);
}

void AppShell::showPopup(const std::string& command)
{
    if (popup.isShown())
        return;

    auto* active = manager.active();

    const auto dir =
        active != nullptr ? active->activeWorkingDirectory() : std::string {};

    popupPrefixArmed = false;
    addSubview(popup);
    popup.setBounds(getLocalBounds());
    popup.show(command, dir);
}

void AppShell::hidePopup()
{
    removeSubview(popup);
    focusAttachedPane();
}

bool AppShell::isLeaderPrefixKey(const KeyEvent& event)
{
    return event.modifiers.control && event.charactersIgnoringModifiers == "a"
           && !event.modifiers.command;
}

bool AppShell::doesKeyTogglePopup(const std::string& characters) const
{
    if (characters == "i")
        return true;

    return std::ranges::any_of(config.bindings,
                               [&](const KeyBinding& binding)
                               { return !binding.popup.empty() && characters == binding.key; });
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
    if (doesKeyTogglePopup(event.charactersIgnoringModifiers))
        popup.dismiss();

    return true;
}

bool AppShell::interceptKey(const KeyEvent& event)
{
    if (prefixArmed)
    {
        prefixArmed = false;
        return handlePrefixed(event);
    }

    // Ctrl+Tab: the MRU session switcher, macOS Cmd+Tab style. Shift reverses.
    // Once it's up it owns the keyboard, so this only fires the first press;
    // the switcher itself handles the rest (further taps, release-to-commit).
    if (isSwitcherKey(event))
    {
        showSwitcher(event.modifiers.shift);
        return true;
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

bool AppShell::isSwitcherKey(const KeyEvent& event)
{
    return event.modifiers.control && !event.modifiers.command
           && event.keyCode == KeyCode::Tab;
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

bool AppShell::runConfigBinding(const std::string& characters)
{
    const auto found = std::ranges::find_if(
        config.bindings,
        [&](const KeyBinding& binding)
        { return !binding.key.empty() && characters == binding.key; });

    if (found == config.bindings.end())
        return false;

    if (!found->send.empty())
        withActivePane([&](TerminalView& pane) { pane.sendText(found->send); });
    else if (!found->popup.empty())
        showPopup(found->popup);

    return true;
}

// Arrows: plain moves focus (tmux select-pane); Ctrl resizes by one
// cell, Alt by five.
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
    const auto& modifiers = event.modifiers;

    withActivePaneTree(
        [&](SessionView& paneTree)
        {
            if (modifiers.control || modifiers.alt)
                paneTree.resizeActive(direction, calculateArrowResizeCells(modifiers.alt));
            else
                paneTree.focusDirection(direction);
        });

    return true;
}

float AppShell::calculateArrowResizeCells(bool altHeld)
{
    constexpr auto arrowResizeCells = 1.0f;
    constexpr auto altArrowResizeCells = 5.0f;

    if (altHeld)
        return altArrowResizeCells;

    return arrowResizeCells;
}

// Shift+H/J/K/L resize by one cell; plain h/j/k/l move focus whether or not
// Ctrl is still down from the prefix roll. Ctrl is left out of resize on
// purpose: rolling the prefix leaves it held, so Ctrl+h must mean "move
// focus", never resize.
bool AppShell::runPaneDirectionKey(const std::string& characters)
{
    constexpr auto letterResizeCells = 1.0f;

    if (const auto direction = findPaneResizeDirection(characters))
    {
        withActivePaneTree([&](SessionView& paneTree)
                           { paneTree.resizeActive(*direction, letterResizeCells); });
        return true;
    }

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

bool AppShell::handlePrefixed(const KeyEvent& event)
{
    // f/w palette, p PR dashboard, a Claude HUD, i lazygit popup, " and %
    // split below and beside, [ ] copy mode and paste, x kills the pane.
    static constexpr auto leaderActions = std::array {
        KeyAction {"f", &AppShell::showPalette},
        KeyAction {"w", &AppShell::showPalette},
        KeyAction {"p", &AppShell::showPrDashboard},
        KeyAction {"a", &AppShell::showClaudeHud},
        KeyAction {"i", &AppShell::showLazygitPopup},
        KeyAction {"\"", &AppShell::splitActivePaneBelow},
        KeyAction {"%", &AppShell::splitActivePaneBeside},
        KeyAction {"z", &AppShell::toggleActivePaneZoom},
        KeyAction {"[", &AppShell::enterCopyModeOnActivePane},
        KeyAction {"]", &AppShell::pasteIntoActivePane},
        KeyAction {"o", &AppShell::cycleActivePaneFocus},
        KeyAction {"c", &AppShell::openSessionInActiveDirectory},
        KeyAction {"x", &AppShell::closeActivePane},
        KeyAction {"^", &AppShell::switchToLastSession},
    };

    const auto& characters = event.charactersIgnoringModifiers;

    // Prefix twice sends a literal Ctrl+A through to the shell.
    if (event.modifiers.control && characters == "a")
        return false;

    // Config bindings first, so ~/.config/cowterm.json can override built-ins.
    if (runConfigBinding(characters) || runArrowKey(event))
        return true;

    if (runPaneDirectionKey(characters) || runKeyAction(leaderActions, characters))
        return true;

    runSessionDigitKey(characters);
    return true;
}

bool AppShell::handleCommand(const KeyEvent& event)
{
    // Zoom (= + - 0) is one size for the whole app: every pane in every
    // session changes together, written back to ~/.config/cowterm.json.
    static constexpr auto commandActions = std::array {
        KeyAction {"k", &AppShell::showPalette},
        KeyAction {"t", &AppShell::showPalette},
        KeyAction {"w", &AppShell::closeActivePane},
        KeyAction {"n", &AppShell::openSessionInActiveDirectory},
        KeyAction {"d", &AppShell::splitActivePaneBeside},
        KeyAction {"q", &AppShell::quitFromLeader},
        KeyAction {"=", &AppShell::increaseGlobalFontSize},
        KeyAction {"+", &AppShell::increaseGlobalFontSize},
        KeyAction {"-", &AppShell::decreaseGlobalFontSize},
        KeyAction {"0", &AppShell::resetGlobalFontSize},
    };

    const auto& characters = event.charactersIgnoringModifiers;
    return runKeyAction(commandActions, characters) || runSessionDigitKey(characters);
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
        manager.newSession(active->activeWorkingDirectory());
}

void AppShell::switchToLastSession()
{
    manager.switchToLast();
}

void AppShell::quitFromLeader()
{
    noteQuit("leader q");
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

void AppShell::resetGlobalFontSize()
{
    setGlobalFontSize(AppConfig {}.fontSize);
}
} // namespace term
