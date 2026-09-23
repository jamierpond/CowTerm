#include "AppShell.h"

#include "CowTermCore/Debug.h"
#include "CowTermVersion.h"
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
    };

    manager.onSessionsChanged = [this] { tray.refresh(); };

    manager.onPaneWired = [this](TermSession& session, TerminalView& pane)
    {
        auto* raw = &session;

        pane.interceptKey = [this](const KeyEvent& event)
        { return interceptKey(event); };

        pane.onTitleChanged = [this, raw](const std::string&)
        {
            if (manager.active() == raw)
                updateTitle();
        };
    };

    tray.onShowWindow = [this] { onBringToFront(); };

    tray.onPickSession = [this](TermSession& session)
    {
        manager.switchTo(session);
        onBringToFront();
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

    palette.onClosed = [this] { hidePalette(); };

    switcher.onClosed = [this] { hideSwitcher(); };

    prDashboard.onClosed = [this] { hidePrDashboard(); };

    claudeHud.onClosed = [this] { hideClaudeHud(); };

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
           || claudeHud.isShown() || popup.isShown();
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
}

void AppShell::showPalette()
{
    if (palette.isShown())
        return;

    showOverlay(palette);
    palette.show();
}

void AppShell::hidePalette()
{
    hideOverlay();

    if (attached != nullptr)
        attached->view.focusActive();
}

void AppShell::showSwitcher(bool reverse)
{
    if (switcher.isShown() || palette.isShown() || popup.isShown())
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

void AppShell::hideSwitcher()
{
    hideOverlay();

    if (attached != nullptr)
        attached->view.focusActive();
}

void AppShell::showPrDashboard()
{
    if (anyOverlayShown())
        return;

    showOverlay(prDashboard);
    prDashboard.show();
}

void AppShell::hidePrDashboard()
{
    hideOverlay();

    if (attached != nullptr)
        attached->view.focusActive();
}

void AppShell::showClaudeHud()
{
    if (anyOverlayShown())
        return;

    showOverlay(claudeHud);
    claudeHud.show();
}

void AppShell::hideClaudeHud()
{
    hideOverlay();

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
            showPopup(binding.popup);
        }

        return true;
    }

    // Shift+H/J/K/L: resize the active pane by one cell. Ctrl is left out
    // of resize on purpose: rolling the prefix leaves it held, so Ctrl+h
    // must mean "move focus", never resize.
    if (chars == "H" || chars == "J" || chars == "K" || chars == "L")
    {
        if (paneTree != nullptr)
            paneTree->resizeActive((char) std::tolower((unsigned char) chars[0]),
                                   1.0f);

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
        if (paneTree != nullptr)
        {
            if (event.modifiers.control || event.modifiers.alt)
                paneTree->resizeActive(arrowDirection,
                                       event.modifiers.alt ? 5.0f : 1.0f);
            else
                paneTree->focusDirection(arrowDirection);
        }

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

    // Lazygit over the active pane's directory, tmux display-popup style;
    // quitting lazygit (or Ctrl+A i again) dismisses it.
    if (chars == "i")
    {
        showPopup("lazygit");
        return true;
    }

    // Pane splits, in the pane's current directory: " below, % beside.
    if (chars == "\"")
    {
        if (paneTree != nullptr)
            paneTree->splitActive(false);

        return true;
    }

    if (chars == "%")
    {
        if (paneTree != nullptr)
            paneTree->splitActive(true);

        return true;
    }

    // Focus moves whether or not Ctrl is still down from the prefix roll.
    if (chars == "h" || chars == "j" || chars == "k" || chars == "l")
    {
        if (paneTree != nullptr)
            paneTree->focusDirection(chars[0]);

        return true;
    }

    if (chars == "z")
    {
        if (paneTree != nullptr)
            paneTree->toggleZoom();

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
        if (paneTree != nullptr)
            paneTree->cycleFocus();

        return true;
    }

    if (chars == "c")
    {
        if (active != nullptr)
            manager.newSession(active->activeWorkingDirectory());

        return true;
    }

    // Kill the active pane; the session ends with its last pane.
    if (chars == "x")
    {
        if (paneTree != nullptr)
            paneTree->closeActivePane();

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
        if (auto* active = manager.active())
            active->view.closeActivePane();

        return true;
    }

    if (chars == "n")
    {
        if (auto* active = manager.active())
            manager.newSession(active->activeWorkingDirectory());

        return true;
    }

    if (chars == "d")
    {
        if (auto* active = manager.active())
            active->view.splitActive(true);

        return true;
    }

    if (chars == "q")
    {
        noteQuit("leader q");
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
