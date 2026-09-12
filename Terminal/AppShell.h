#pragma once

#include "ClaudeHud.h"
#include "Config.h"
#include "Palette.h"
#include "Popup.h"
#include "PrDashboard.h"
#include "Session.h"
#include "Switcher.h"
#include "TrayController.h"

#include <eacp/UI/UI.h>

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
    // The overlays are UI components, not native views: one ComponentHost (a
    // GPUView) carries whichever is open, which is why they raise and lower as
    // a single subview rather than one apiece.
    void showOverlay(eacp::UI::Component& overlay);
    void hideOverlay();
    void raiseOverlay();
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
    bool anyOverlayShown() const;
    void attachActive(TermSession& session);
    void setGlobalFontSize(float size);
    void updateTitle();
    void handleSessionNotify(TermSession& session, const std::string& text);

    AppConfig config = loadConfig();
    SessionManager manager {config};
    Palette palette {config, manager};
    Switcher switcher {config, manager};
    PrDashboard prDashboard {config, manager};
    ClaudeHud claudeHud {config, manager};
    Popup popup {config};
    eacp::UI::ComponentHost overlayHost;
    TrayController tray {manager};
    TermSession* attached = nullptr;
    bool prefixArmed = false;
    bool popupPrefixArmed = false;
    bool windowFocused = true;
};
} // namespace term
