#pragma once

#include "OverlayPanel.h"
#include "PaletteItem.h"

#include <cstdint>
#include <string>
#include <vector>

namespace term
{
class RemoteHost;

// The switcher: one overlay that fuzzy-searches everything — open sessions
// (Claude sessions surfaced with their conversation title and last notify),
// then known project dirs that aren't open yet. Empty query lists by
// recency, Grazer-style; Enter switches or spawns. CPU-painted chrome over the
// GPU terminal.
class Palette final : public SearchablePanel
{
public:
    Palette(const AppConfig& configToUse, SessionManager& sessionsToUse);

    void show();

    // A remote host answered (or failed): rebuild the rows if shown.
    void refreshRemoteRows();
    bool isShown() const { return shown; }

    // Fired for both dismissal and selection; the shell removes the overlay
    // and restores terminal focus.
    eacp::Callback onClosed = [] {};

    void paint(eacp::UI::Graphics& g) override;
    bool keyDown(const eacp::UI::KeyEvent& event) override;
    void mouseMove(const eacp::UI::MouseEvent& event) override;

private:
    void logPick(const PaletteItem& item) const;
    void logCancel() const;
    std::int64_t measureMsSinceOpen() const;

    void rebuild();
    PaletteItem makeSessionItem(TermSession& session) const;
    PaletteItem makeProjectItem(const std::string& name, const std::string& path) const;
    void addRemoteItems();
    PaletteItem makeUnreachableItem(const RemoteHost& remote) const;
    void applyQuery();
    void chooseSelected() override;
    void dismiss() override;
    void peekSelected();
    int countRows() const override;
    void moveSelection(int delta) override;
    void searchTextChanged() override;
    bool handleNavigationKey(const eacp::UI::KeyEvent& event);
    int findRowAt(eacp::UI::Point pos) const override;
    eacp::UI::Rect calculatePanelBounds() const override;

    void paintRow(eacp::UI::Graphics& g, const PaletteItem& item, eacp::UI::Rect rowArea);

    std::vector<PaletteItem> allItems;
    std::vector<PaletteItem> visible;
    bool shown = false;
    int keysSinceOpen = 0;
    std::int64_t openedAtMs = 0;
};
} // namespace term
