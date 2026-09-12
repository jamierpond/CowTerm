#pragma once

#include "Config.h"

#include <eacp/UI/UI.h>
#include "Session.h"

#include <eacp/Core/Threads/Timer.h>

#include <memory>
#include <string>
#include <vector>

namespace term
{
// The Claude HUD (Ctrl+A a): every Claude Code instance running in any
// session, in one place — which session it lives in, its conversation title,
// its working directory, and its last notification (highlighted while it
// hasn't been looked at, so "Claude is waiting on you" is one glance).
// Rows needing attention sort first. Enter jumps to that session and focuses
// the pane the conversation runs in; while shown the list refreshes itself
// once a second, so state changes appear live.
class ClaudeHud final : public eacp::UI::Component
{
public:
    ClaudeHud(const AppConfig& configToUse, SessionManager& sessionsToUse);

    void show();
    bool isShown() const { return shown; }

    eacp::Callback onClosed = [] {};

    void paint(eacp::UI::Graphics& g) override;
    bool keyDown(const eacp::UI::KeyEvent& event) override;
    void mouseDown(const eacp::UI::MouseEvent& event) override;
    void mouseMove(const eacp::UI::MouseEvent& event) override;

private:
    struct Item
    {
        std::string sessionKey;
        std::string sessionName;
        TerminalView* pane = nullptr;
        std::string title;
        std::string cwd;
        std::string notify;
        std::int64_t notifyAt = 0;
        bool attention = false;
        std::int64_t lastUsed = 0;
    };

    void rebuild();
    void choose();
    void cancel();
    void moveSelection(int delta);
    int rowAt(eacp::UI::Point pos) const;
    eacp::UI::Rect panelBounds() const;

    const AppConfig& config;
    SessionManager& sessions;
    Theme theme;

    std::vector<Item> items;
    int selected = 0;
    bool shown = false;

    // Live refresh while the HUD is up: titles, notifications and process
    // state move underneath us as conversations progress.
    std::unique_ptr<eacp::Threads::Timer> tick;

    eacp::UI::Font headerFont;
    eacp::UI::Font rowFont;
    eacp::UI::Font detailFont;
};
} // namespace term
