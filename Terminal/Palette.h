#pragma once

#include "OverlayPanel.h"
#include "Multiplexer/PaletteItem.h"
#include "Multiplexer/ActivityReports.h"

#include <cstdint>
#include <string>
#include <vector>

namespace term
{
class RemoteHost;

// The switcher: one overlay that fuzzy-searches everything — open sessions
// (Claude sessions surfaced with their conversation title and last notify),
// then known project dirs that aren't open yet. Empty query lists unread
// sessions first (newest first), then by recency, Grazer-style; a typed query
// ranks by the match alone, the unread pill shown but not moving a row; Enter switches or spawns. CPU-painted chrome over the
// GPU terminal.
class Palette final : public SearchablePanel
{
public:
    Palette(const AppConfig& configToUse,
            const HostRowColours& thisMachineColours,
            SessionManager& sessionsToUse,
            const ActivityReports& activityReportsToUse);

    void show();

    // A remote host answered (or failed): rebuild the rows if shown.
    void refreshRemoteRows();

    // The rows the palette lists, built now: every session, then the projects
    // not open here, then the remote ones.
    std::vector<PaletteItem> collectItems() const;
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
    void addRemoteItems(std::vector<PaletteItem>& items) const;
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

    void paintRow(eacp::UI::Graphics& g,
                  const PaletteItem& item,
                  eacp::UI::Rect rowArea,
                  const HostRowColours& colours);

    float paintRowBadges(eacp::UI::Graphics& g,
                         const PaletteItem& item,
                         eacp::UI::Rect rowArea,
                         const HostRowColours& colours);

    const ActivityReports& activityReports;
    std::vector<PaletteItem> allItems;
    std::vector<PaletteItem> visible;
    bool shown = false;
    int keysSinceOpen = 0;
    std::int64_t openedAtMs = 0;
};
} // namespace term
