#pragma once

#include "Config.h"
#include "Permissions.h"

#include <eacp/Core/Threads/Timer.h>
#include <eacp/UI/UI.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace term
{
// CowTerm > Permissions… (macOS): one row per privacy grant with its live
// status and a Grant button, and under the selected row Rudder's unlock
// card -- the draggable app icon for lists that take one, and the steps.
//
// macOS sends nothing when a grant changes, so the panel checks once a
// second, and only while it is shown: a closed panel never wakes CowTerm.
class PermissionsPanel final : public eacp::UI::Component
{
public:
    explicit PermissionsPanel(const AppConfig& configToUse);
    ~PermissionsPanel() override;

    // `confirmReset` opens straight onto the reset confirmation.
    void show(bool confirmReset = false);
    bool isShown() const { return shown; }

    eacp::Callback onClosed = [] {};

    void paint(eacp::UI::Graphics& g) override;
    bool keyDown(const eacp::UI::KeyEvent& event) override;
    void mouseDown(const eacp::UI::MouseEvent& event) override;
    void mouseMove(const eacp::UI::MouseEvent& event) override;

private:
    struct Layout
    {
        eacp::UI::Rect card;
        std::vector<eacp::UI::Rect> rows;
        std::vector<eacp::UI::Rect> actions;
        eacp::UI::Rect detail;
        eacp::UI::Rect tile;
        eacp::UI::Rect banner;
        eacp::UI::Rect relaunch;
        eacp::UI::Rect reset;
        eacp::UI::Rect confirm;
        eacp::UI::Rect cancel;
    };

    Layout layout() const;
    bool bannerShown() const;
    permissions::Status statusOf(permissions::Kind kind) const;
    const permissions::Info& selectedInfo() const;

    void refresh();
    permissions::Status checkGrant(const permissions::Info& grant) const;
    void act(int row);
    void resetAll();
    void close();
    void cancelOrClose();
    void confirmOrAct();
    void handleConfirmingResetClick(const Layout& at, eacp::UI::Point pos);
    bool handleRowClick(const Layout& at, eacp::UI::Point pos);
    bool handleButtonClick(const Layout& at, eacp::UI::Point pos);
    void loadAppIconOnce();
    bool isDragTileActive() const;
    std::string describeBanner() const;

    void paintRow(eacp::UI::Graphics& g, const Layout& at, int row);
    eacp::UI::Color getStatusColour(permissions::Status status) const;
    void paintDetail(eacp::UI::Graphics& g, const Layout& at);
    void paintDragTile(eacp::UI::Graphics& g, const eacp::UI::Rect& tile, bool done);
    void paintTitle(eacp::UI::Graphics& g, const eacp::UI::Rect& card);
    void paintBanner(eacp::UI::Graphics& g, const Layout& at);
    void paintFooter(eacp::UI::Graphics& g, const Layout& at);
    void paintButton(eacp::UI::Graphics& g,
                     const eacp::UI::Rect& area,
                     const std::string& label,
                     eacp::UI::Color accent);

    const AppConfig& config;
    Theme theme;

    std::vector<permissions::Entry> entries;
    std::set<permissions::Kind> prompted;


    bool shown = false;
    bool confirmingReset = false;

    // Since a reset this process's cached grants are stale until it relaunches.
    bool resetDone = false;
    std::string error;
    int selected = 0;
    eacp::UI::Point mouse;

    std::unique_ptr<eacp::Threads::Timer> tick;

    // Outlives the panel's callbacks, so a late answer from a background
    // check lands nowhere once the panel is gone.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    eacp::UI::ImageRef icon;
    eacp::UI::Font titleFont;
    eacp::UI::Font rowFont;
    eacp::UI::Font detailFont;
};
} // namespace term
