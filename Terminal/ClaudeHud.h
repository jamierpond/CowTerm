#pragma once

#include "OverlayPanel.h"

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
class ClaudeHud final : public OverlayPanel
{
public:
    ClaudeHud(const AppConfig& configToUse, SessionManager& sessionsToUse);

    void show();
    bool isShown() const { return shown; }

    eacp::Callback onClosed = [] {};

    void paint(eacp::UI::Graphics& g) override;
    bool keyDown(const eacp::UI::KeyEvent& event) override;
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
    void sortByAttentionThenRecency();
    void keepSelectionOn(const TerminalView* pane);
    void chooseSelected() override;
    void dismiss() override;
    void moveSelection(int delta);
    int findRowAt(eacp::UI::Point pos) const override;
    eacp::UI::Rect calculatePanelBounds() const override;

    void paintHeader(eacp::UI::Graphics& g, eacp::UI::Rect panel);
    void paintRow(eacp::UI::Graphics& g,
                  const Item& item,
                  eacp::UI::Rect rowArea,
                  std::int64_t now);
    std::string calculateSummary() const;
    int countWaitingInstances() const;

    std::vector<Item> items;
    bool shown = false;

    // Live refresh while the HUD is up: titles, notifications and process
    // state move underneath us as conversations progress.
    std::unique_ptr<eacp::Threads::Timer> tick;
};
} // namespace term
