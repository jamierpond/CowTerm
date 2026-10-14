#pragma once

#include "Config.h"
#include "Permissions.h"

#include <eacp/Core/Threads/Timer.h>
#include <eacp/UI/UI.h>

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace term
{
// CowTerm > Permissions… (macOS): one row per privacy grant with its live
// status and a Grant button, and under the selected row Rudder's unlock
// card -- the draggable app icon for lists that take one, and the steps. It
// opens by itself on launch while a required grant is missing, and then goes
// away by itself once they are all in force.
//
// macOS sends nothing when a grant changes, so the panel checks once a
// second, and only while it is shown: a closed panel never wakes CowTerm.
class PermissionsPanel final : public eacp::UI::Component
{
public:
    explicit PermissionsPanel(const AppConfig& configToUse);
    ~PermissionsPanel() override;

    // `automatic` is the launch-time showing, the one that dismisses itself.
    // `confirmReset` opens straight onto the reset confirmation.
    void show(bool automatic, bool confirmReset = false);
    bool isShown() const { return shown; }

    // Whether launch should show the panel: a required grant is missing.
    static bool requiredMissingNow();

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
    void act(int row);
    void resetAll();
    void close();

    void paintRow(eacp::UI::Graphics& g, const Layout& at, int row);
    void paintDetail(eacp::UI::Graphics& g, const Layout& at);
    void paintButton(eacp::UI::Graphics& g,
                     const eacp::UI::Rect& area,
                     const std::string& label,
                     eacp::UI::Color accent);

    const AppConfig& config;
    Theme theme;

    std::vector<permissions::Entry> entries;
    std::set<permissions::Kind> prompted;

    // The fresh-process answer for Screen Recording (see probeScreenRecording)
    // and whether one is already running.
    std::optional<bool> screenProbe;
    bool probing = false;
    int ticks = 0;

    bool shown = false;
    bool automatic = false;
    bool confirmingReset = false;

    // Since a reset this process's cached grants are stale, so Screen
    // Recording is believed only from a fresh process.
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
