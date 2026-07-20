#pragma once

#include "Config.h"
#include "Session.h"

#include <eacp/Core/Threads/Timer.h>

#include <memory>
#include <vector>

namespace term
{
// The Ctrl+Tab session switcher: a compact, centered HUD in the vein of the
// macOS Cmd+Tab app switcher. Opening snapshots the sessions in
// most-recently-used order and highlights the *previous* one, so a quick
// press-and-release flips straight to it. Holding Ctrl and tapping Tab walks
// the frozen order (Shift reverses); releasing Ctrl commits the highlight.
// The order never re-sorts mid-walk — only the committed session floats to the
// top of the MRU, ready for the next flip.
class Switcher final : public eacp::Graphics::View
{
public:
    Switcher(const AppConfig& configToUse, SessionManager& sessionsToUse);

    // Open pointing at the next MRU entry (the previous session), or the last
    // one when reverse. Returns false and stays closed when there aren't at
    // least two sessions to flip between.
    bool begin(bool reverse);

    bool isShown() const { return shown; }

    // A further Ctrl+Tab / Ctrl+Shift+Tab while already open: advance the
    // highlight one step forward (or back), wrapping.
    void step(bool reverse);

    // Fired on commit or cancel; the shell removes the overlay and restores
    // terminal focus.
    eacp::Callback onClosed = [] {};

    void paint(eacp::Graphics::Context& context) override;
    void keyDown(const eacp::Graphics::KeyEvent& event) override;
    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    void mouseMoved(const eacp::Graphics::MouseEvent& event) override;

private:
    void commit();
    void cancel();
    void peekSelected();
    void stopPolling();

    float cardWidth() const;
    eacp::Graphics::Rect panelBounds() const;
    int cardAt(eacp::Graphics::Point pos) const;

    const AppConfig& config;
    SessionManager& sessions;
    Theme theme;

    // Frozen MRU snapshot taken on begin(); index 0 is the current session.
    std::vector<TermSession*> order;
    int selected = 0;
    bool shown = false;

    eacp::Graphics::Font titleFont;
    eacp::Graphics::Font labelFont;
    eacp::Graphics::Font detailFont;

    // Polls the modifier keys while open so releasing Ctrl commits, the way the
    // macOS app switcher commits when you let go of Cmd. eacp no longer pushes
    // modifier-change events, so we watch the state instead.
    std::unique_ptr<eacp::Threads::Timer> modifierPoll;
};
} // namespace term
