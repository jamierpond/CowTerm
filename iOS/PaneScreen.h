#pragma once

#include "PaneScreenChrome.h"
#include "PaneScreenInputs.h"

#include "TerminalView/TerminalView.h"

#include <eacp/Core/Threads/Timer.h>
#include <eacp/UI/UI.h>

#include <optional>

// The phone's pane screen (docs/iOSDesign.md, "The pane screen"): one pane
// inside the safe area, the top line, the bottom bar above the home
// indicator, the "↓ n new" pill and the long press menu. The shell behind
// the pane comes from the TerminalConfig it is given; the link state and the
// pane's description come in through setLinkStatus and setPaneDescription.
namespace cowterm::mobile
{
// Takes the finger on the pane: a drag scrolls the terminal with momentum, a
// tap reaches it as a click, which opens a link or clicks in a mouse program.
class PaneTouchSurface final : public eacp::Graphics::View
{
public:
    explicit PaneTouchSurface(term::TerminalView& terminalToUse);

    void touchBegan(const eacp::Graphics::TouchEvent& event) override;
    void touchMoved(const eacp::Graphics::TouchEvent& event) override;
    void touchEnded(const eacp::Graphics::TouchEvent& event) override;

private:
    void clickTerminalAt(const eacp::Graphics::TouchEvent& event);

    term::TerminalView& terminal;
    std::optional<eacp::Graphics::TouchEvent> fingerDown;
    bool isScrolling = false;
};

struct PaneScreenActions
{
    std::function<void()> onPreviousSession = [] {};
    std::function<void(int paneIndex)> onChoosePane = [](int) {};
};

class PaneScreen final : public eacp::Graphics::View
{
public:
    // paneId names the daemon's pane to show; empty for a local shell.
    PaneScreen(const term::TerminalConfig& config, const std::string& paneId);

    void setPaneDescription(const PaneDescription& pane);
    void setLinkStatus(const LinkStatus& link);

    // From WindowEvents::onSoftwareKeyboardChanged: lays the screen out above
    // the keyboard and learns the keyboard-up row count from it.
    void setSoftwareKeyboardChange(const eacp::Graphics::SoftwareKeyboardChange& change);

    // Fixes the pane's grid in portrait: columns from the pane's width, rows
    // from its keyboard-up height.
    void fixGridAtKeyboardUpSize();

    // What a long press on the bottom bar opens.
    void openMenu();

    void resized() override;
    void safeAreaInsetsChanged() override;

    term::TerminalView terminal;
    PaneScreenActions actions;

private:
    void layOutParts();
    void showSoftwareKeyboard();
    void returnToLiveBottom();
    void refreshWhatChangesOverTime();
    void showScrolledBackState(term::ScrolledBackState state);
    bool isLandscape() const;
    void closeMenu();
    std::vector<MenuAction> listMenuActions();
    eacp::Graphics::Insets calculateInsetsAroundParts() const;
    bool isLive() const;

    term::Theme theme;
    PaneDescription pane;
    LinkStatus link;
    float softwareKeyboardCoveredHeight = 0.0f;
    std::optional<float> portraitSoftwareKeyboardHeight;

    PaneTouchSurface touchSurface {terminal};

    eacp::UI::ComponentHost topLineHost;
    TopLine topLine {theme};

    eacp::UI::ComponentHost bottomBarHost;
    BottomBar bottomBar {theme};

    eacp::UI::ComponentHost pillHost;
    NewLinesPill pill {theme};

    eacp::UI::ComponentHost menuHost;
    ActionMenu menu {theme};

    eacp::Threads::Timer refreshTimer;
};
} // namespace cowterm::mobile
