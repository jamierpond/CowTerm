#include "AppShell.h"
#include "GitWorktree.h"
#include "Notifier.h"

#include <eacp/Core/App/App.h>

#include <algorithm>
#include <cctype>

namespace term
{
using namespace eacp;
using Graphics::KeyEvent;
namespace KeyCode = Graphics::KeyCode;

AppShell::AppShell()
{
    manager.onActiveChanged = [this](TermSession& session)
    {
        attachActive(session);
        updateTitle();
        tray.refresh();

        // Fires on focus moves and pane-tree changes too — the web/remote
        // clients' roster (active markers, closed panes) rides on it.
        web.sessionsChanged();
    };

    manager.onSessionsChanged = [this]
    {
        tray.refresh();
        web.sessionsChanged();
    };

    manager.onPaneWired = [this](TermSession& session, TerminalView& pane)
    {
        auto* raw = &session;

        pane.interceptKey = [this](const KeyEvent& event)
        { return interceptKey(event); };

        pane.onTitleChanged = [this, raw](const std::string&)
        {
            if (manager.active() == raw)
                updateTitle();

            web.sessionsChanged();
        };

        web.wirePane(pane);
        web.sessionsChanged();
    };

    tray.onShowWindow = [this] { onBringToFront(); };

    tray.onPickSession = [this](TermSession& session)
    {
        manager.switchTo(session);
        onBringToFront();
    };

    manager.onAllClosed = [] { Apps::quit(); };

    manager.onNotify = [this](TermSession& session, const std::string& text)
    {
        handleSessionNotify(session, text);
        tray.refresh();
    };

    palette.onClosed = [this] { hidePalette(); };

    switcher.onClosed = [this] { hideSwitcher(); };

    prDashboard.onClosed = [this] { hidePrDashboard(); };

    claudeHud.onClosed = [this] { hideClaudeHud(); };

    remoteHud.onClosed = [this] { hideRemoteHud(); };

    // A chosen remote session becomes a real session here: the whole pane
    // tree, every leaf a live RemoteShell, switchable like any other.
    remoteHud.onOpenSession =
        [this](web::GatewayClient& client, const web::wire::SessionInfo& info)
    { manager.openRemoteSession(client, info); };

    // The owner reshaped or ended a session we mirror; follow it.
    fleet.onChanged = [this](web::GatewayClient& client)
    {
        manager.refreshRemoteSessions(client);

        // A session we asked the remote to create has now appeared.
        if (pendingMirror.client == &client)
        {
            for (const auto& info: client.sessions())
                if (info.key == pendingMirror.key)
                {
                    pendingMirror = {};
                    manager.openRemoteSession(client, info);
                    break;
                }
        }

        remoteHud.remoteChanged();
    };

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

    popup.onClosed = [this] { hidePopup(); };

    popup.interceptKey = [this](const KeyEvent& event) { return popupKey(event); };

    Notifier::initialize(
        [this](const std::string& sessionKey)
        {
            if (auto* session = manager.find(sessionKey))
            {
                manager.switchTo(*session);
                onBringToFront();
            }
        });
}

void AppShell::start()
{
    manager.restoreOrCreateInitial();
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
        // re-raise the overlay so it stays on top and keeps keyboard focus.
        if (palette.isShown())
        {
            removeSubview(palette);
            addSubview(palette);
            palette.focus();
        }
        else if (switcher.isShown())
        {
            removeSubview(switcher);
            addSubview(switcher);
            switcher.focus();
        }
        else if (prDashboard.isShown())
        {
            removeSubview(prDashboard);
            addSubview(prDashboard);
            prDashboard.focus();
        }
        else if (claudeHud.isShown())
        {
            removeSubview(claudeHud);
            addSubview(claudeHud);
            claudeHud.focus();
        }
    }

    session.view.setBounds(getLocalBounds());

    if (!anyOverlayShown())
        session.view.focusActive();
}

bool AppShell::anyOverlayShown() const
{
    return palette.isShown() || switcher.isShown() || prDashboard.isShown()
           || claudeHud.isShown() || remoteHud.isShown() || popup.isShown();
}

void AppShell::resized()
{
    const auto bounds = getLocalBounds();

    if (attached != nullptr)
        attached->view.setBounds(bounds);

    palette.setBounds(bounds);
    switcher.setBounds(bounds);
    prDashboard.setBounds(bounds);
    claudeHud.setBounds(bounds);
    remoteHud.setBounds(bounds);
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

    onWindowTitleChanged(title);
}

void AppShell::handleSessionNotify(TermSession& session, const std::string& text)
{
    // The active, focused session is right in front of the user — no need
    // to shout about it.
    const auto* pane = const_cast<const SessionView&>(session.view).activePane();

    if (windowFocused && manager.active() == &session && pane != nullptr
        && pane->hasFocus())
        return;

    Notifier::notify(session.key(), session.name, text);
}

void AppShell::showPalette()
{
    if (palette.isShown())
        return;

    addSubview(palette);
    palette.setBounds(getLocalBounds());
    palette.show();
    palette.focus();
}

void AppShell::hidePalette()
{
    removeSubview(palette);

    if (attached != nullptr)
        attached->view.focusActive();
}

void AppShell::showSwitcher(bool reverse)
{
    if (switcher.isShown() || palette.isShown() || popup.isShown())
        return;

    addSubview(switcher);
    switcher.setBounds(getLocalBounds());

    // begin() only opens with two or more sessions to flip between; if it
    // declines, take the overlay back down and leave the pane focused.
    if (!switcher.begin(reverse))
    {
        removeSubview(switcher);
        return;
    }

    switcher.focus();
}

void AppShell::hideSwitcher()
{
    removeSubview(switcher);

    if (attached != nullptr)
        attached->view.focusActive();
}

void AppShell::showPrDashboard()
{
    if (anyOverlayShown())
        return;

    addSubview(prDashboard);
    prDashboard.setBounds(getLocalBounds());
    prDashboard.show();
    prDashboard.focus();
}

void AppShell::hidePrDashboard()
{
    removeSubview(prDashboard);

    if (attached != nullptr)
        attached->view.focusActive();
}

void AppShell::showClaudeHud()
{
    if (anyOverlayShown())
        return;

    addSubview(claudeHud);
    claudeHud.setBounds(getLocalBounds());
    claudeHud.show();
    claudeHud.focus();
}

void AppShell::hideClaudeHud()
{
    removeSubview(claudeHud);

    if (attached != nullptr)
        attached->view.focusActive();
}

void AppShell::runSessionCommand(SessionCommand command, float cells)
{
    auto* active = manager.active();

    if (active == nullptr)
        return;

    // The pane tree belongs to whoever owns the shells in it. Sending the
    // command rather than applying it keeps the two views identical: the
    // owner reshapes, broadcasts, and the mirror follows.
    if (isShapeCommand(command) && active->isRemote())
    {
        if (auto* pane = active->view.activePane())
            active->remoteClient->command(pane->shellId(), command, cells);

        return;
    }

    applySessionCommand(active->view, command, cells);
}

void AppShell::runPopupCommand(const std::string& command)
{
    auto* active = manager.active();

    if (active == nullptr || popup.isShown())
        return;

    if (!active->isRemote())
    {
        showPopup(command);
        return;
    }

    // A mirror's directory lives on the other machine, so running the
    // command here would open it against a path that may not even exist.
    // It runs over there, in a pane made for us, and displays here.
    auto* pane = active->view.activePane();

    if (pane == nullptr)
        return;

    const auto& screen = pane->screenModel();
    auto* client = active->remoteClient;

    // No lifetime guard needed: this callback is stored in the fleet's
    // client, the fleet is a member of this shell, so it cannot outlive us.
    client->popup(pane->shellId(),
                  command,
                  screen.columns(),
                  screen.rows(),
                  [this, client](const std::string& ephemeralPane)
                  {
                      if (popup.isShown())
                          return;

                      popupPrefixArmed = false;
                      addSubview(popup);
                      popup.setBounds(getLocalBounds());
                      popup.showShell(std::make_unique<web::RemoteShell>(
                          *client, ephemeralPane, /*ownsSize*/ true));
                  });
}

void AppShell::newSessionHere()
{
    auto* active = manager.active();

    if (active == nullptr)
        return;

    if (!active->isRemote())
    {
        manager.newSession(active->activeWorkingDirectory());
        return;
    }

    auto* client = active->remoteClient;

    // The remote answers with the new key before its roster push (those
    // are coalesced), so record the intent and mirror it once the session
    // actually shows up — see the fleet.onChanged handler.
    client->open(active->activeWorkingDirectory(),
                 [this, client](const std::string& key)
                 { pendingMirror = {client, key}; });
}

void AppShell::detachMirror()
{
    if (auto* active = manager.active(); active != nullptr && active->isRemote())
        manager.close(*active);
}

void AppShell::showRemoteHud()
{
    if (anyOverlayShown())
        return;

    addSubview(remoteHud);
    remoteHud.setBounds(getLocalBounds());
    remoteHud.show();
    remoteHud.focus();
}

void AppShell::hideRemoteHud()
{
    removeSubview(remoteHud);

    if (attached != nullptr)
        attached->view.focusActive();
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

    if (attached != nullptr)
        attached->view.focusActive();
}

bool AppShell::popupKey(const KeyEvent& event)
{
    if (popupPrefixArmed)
    {
        popupPrefixArmed = false;

        if (event.modifiers.control && event.charactersIgnoringModifiers == "a")
            return false;

        const auto togglesPopup = [&](const std::string& chars)
        {
            if (chars == "i")
                return true;

            for (const auto& binding: config.bindings)
                if (!binding.popup.empty() && chars == binding.key)
                    return true;

            return false;
        };

        if (togglesPopup(event.charactersIgnoringModifiers))
        {
            popup.dismiss();
            return true;
        }

        // The rest of the leader table targets panes hidden behind the
        // popup; swallow rather than mutate what the user can't see.
        return true;
    }

    if (event.modifiers.control && event.charactersIgnoringModifiers == "a"
        && !event.modifiers.command)
    {
        popupPrefixArmed = true;
        return true;
    }

    return false;
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
    if (event.modifiers.control && !event.modifiers.command
        && event.keyCode == KeyCode::Tab)
    {
        showSwitcher(event.modifiers.shift);
        return true;
    }

    if (event.modifiers.control && event.charactersIgnoringModifiers == "a"
        && !event.modifiers.command)
    {
        prefixArmed = true;
        return true;
    }

    if (event.modifiers.command)
        return handleCommand(event);

    return false;
}

bool AppShell::handlePrefixed(const KeyEvent& event)
{
    const auto& chars = event.charactersIgnoringModifiers;
    auto* active = manager.active();
    auto* paneTree = active != nullptr ? &active->view : nullptr;

    // Prefix twice sends a literal Ctrl+A through to the shell.
    if (event.modifiers.control && chars == "a")
        return false;

    // Config bindings first, so ~/.config/cowterm.json can override built-ins.
    for (const auto& binding: config.bindings)
    {
        if (binding.key.empty() || chars != binding.key)
            continue;

        if (!binding.send.empty())
        {
            if (paneTree != nullptr)
                if (auto* pane = paneTree->activePane())
                    pane->sendText(binding.send);
        }
        else if (!binding.popup.empty())
        {
            runPopupCommand(binding.popup);
        }

        return true;
    }

    // Shift+H/J/K/L: resize the active pane by one cell. Ctrl is left out
    // of resize on purpose: rolling the prefix leaves it held, so Ctrl+h
    // must mean "move focus", never resize.
    if (chars == "H" || chars == "J" || chars == "K" || chars == "L")
    {
        runSessionCommand(chars == "H"   ? SessionCommand::ResizeLeft
                          : chars == "J" ? SessionCommand::ResizeDown
                          : chars == "K" ? SessionCommand::ResizeUp
                                         : SessionCommand::ResizeRight);
        return true;
    }

    // Arrows: plain moves focus (tmux select-pane); Ctrl resizes by one
    // cell, Alt by five.
    const auto arrowDirection = [&]() -> char
    {
        switch (event.keyCode)
        {
            case KeyCode::LeftArrow:
                return 'h';
            case KeyCode::DownArrow:
                return 'j';
            case KeyCode::UpArrow:
                return 'k';
            case KeyCode::RightArrow:
                return 'l';
            default:
                return 0;
        }
    }();

    if (arrowDirection != 0)
    {
        const auto resizing = event.modifiers.control || event.modifiers.alt;

        const auto command = [&]
        {
            switch (arrowDirection)
            {
                case 'h':
                    return resizing ? SessionCommand::ResizeLeft
                                    : SessionCommand::FocusLeft;
                case 'j':
                    return resizing ? SessionCommand::ResizeDown
                                    : SessionCommand::FocusDown;
                case 'k':
                    return resizing ? SessionCommand::ResizeUp
                                    : SessionCommand::FocusUp;
                default:
                    return resizing ? SessionCommand::ResizeRight
                                    : SessionCommand::FocusRight;
            }
        }();

        runSessionCommand(command, event.modifiers.alt ? 5.0f : 1.0f);
        return true;
    }

    if (chars == "f" || chars == "w")
    {
        showPalette();
        return true;
    }

    // The PR dashboard, on the same key the tmux setup bound it to.
    if (chars == "p")
    {
        showPrDashboard();
        return true;
    }

    // The Claude HUD: every conversation across every session, one glance.
    if (chars == "a")
    {
        showClaudeHud();
        return true;
    }

    // The remote HUD: other CowTerms on the network, ready to pilot.
    if (chars == "r")
    {
        showRemoteHud();
        return true;
    }

    // Lazygit over the active pane's directory, tmux display-popup style;
    // quitting lazygit (or Ctrl+A i again) dismisses it.
    if (chars == "i")
    {
        runPopupCommand("lazygit");
        return true;
    }

    // Pane splits, in the pane's current directory: " below, % beside.
    if (chars == "\"")
    {
        runSessionCommand(SessionCommand::SplitBelow);
        return true;
    }

    if (chars == "%")
    {
        runSessionCommand(SessionCommand::SplitBeside);
        return true;
    }

    // Focus moves whether or not Ctrl is still down from the prefix roll.
    if (chars == "h" || chars == "j" || chars == "k" || chars == "l")
    {
        runSessionCommand(chars == "h"   ? SessionCommand::FocusLeft
                          : chars == "j" ? SessionCommand::FocusDown
                          : chars == "k" ? SessionCommand::FocusUp
                                         : SessionCommand::FocusRight);
        return true;
    }

    if (chars == "z")
    {
        runSessionCommand(SessionCommand::ToggleZoom);
        return true;
    }

    // tmux copy mode on the active pane; ] pastes, completing the pair.
    if (chars == "[")
    {
        if (paneTree != nullptr)
            if (auto* pane = paneTree->activePane())
                pane->enterCopyMode();

        return true;
    }

    if (chars == "]")
    {
        if (paneTree != nullptr)
            if (auto* pane = paneTree->activePane())
                pane->paste();

        return true;
    }

    if (chars == "o")
    {
        runSessionCommand(SessionCommand::CycleFocus);
        return true;
    }

    if (chars == "c")
    {
        newSessionHere();
        return true;
    }

    // Kill the active pane; the session ends with its last pane. On a
    // mirror this closes the pane over there and the mirror follows.
    if (chars == "x")
    {
        runSessionCommand(SessionCommand::ClosePane);
        return true;
    }

    // Stop viewing a mirror, leaving everything running over there.
    if (chars == "D")
    {
        detachMirror();
        return true;
    }

    if (chars == "^")
    {
        manager.switchToLast();
        return true;
    }

    if (chars.size() == 1 && chars[0] >= '1' && chars[0] <= '9')
    {
        manager.switchToIndex(chars[0] - '1');
        return true;
    }

    return true;
}

bool AppShell::handleCommand(const KeyEvent& event)
{
    const auto& chars = event.charactersIgnoringModifiers;

    if (chars == "k" || chars == "t")
    {
        showPalette();
        return true;
    }

    if (chars == "w")
    {
        runSessionCommand(SessionCommand::ClosePane);
        return true;
    }

    if (chars == "n")
    {
        newSessionHere();
        return true;
    }

    if (chars == "d")
    {
        runSessionCommand(SessionCommand::SplitBeside);
        return true;
    }

    if (chars == "q")
    {
        Apps::quit();
        return true;
    }

    // Zoom: one size for the whole app. Every pane in every session changes
    // together, and the new size is written back to ~/.config/cowterm.json.
    if (chars == "=" || chars == "+")
    {
        setGlobalFontSize(config.fontSize + 1);
        return true;
    }

    if (chars == "-")
    {
        setGlobalFontSize(config.fontSize - 1);
        return true;
    }

    if (chars == "0")
    {
        setGlobalFontSize(AppConfig {}.fontSize);
        return true;
    }

    if (chars.size() == 1 && chars[0] >= '1' && chars[0] <= '9')
    {
        manager.switchToIndex(chars[0] - '1');
        return true;
    }

    return false;
}
} // namespace term
