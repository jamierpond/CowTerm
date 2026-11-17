#pragma once

#include "PaneScreenChrome.h"
#include "OutputWatchingShell.h"
#include "PaneScreenInputs.h"
#include "TypingControls.h"

#include "PasteConfirmation.h"
#include "TerminalView/TerminalView.h"

#include <eacp/Core/Threads/Timer.h>
#include <eacp/UI/UI.h>

#include <chrono>
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
    explicit PaneTouchSurface(std::function<term::TerminalView&()> findTerminalToUse);

    void touchBegan(const eacp::Graphics::TouchEvent& event) override;
    void touchMoved(const eacp::Graphics::TouchEvent& event) override;
    void touchEnded(const eacp::Graphics::TouchEvent& event) override;

    std::function<void()> onSwipeFromTheLeftEdge = [] {};

private:
    void clickTerminalAt(const eacp::Graphics::TouchEvent& event);

    std::function<term::TerminalView&()> findTerminal;
    std::optional<eacp::Graphics::TouchEvent> fingerDown;
    bool isScrolling = false;
};

struct PaneScreenActions
{
    std::function<void()> onPreviousSession = [] {};
    std::function<void(int paneIndex)> onChoosePane = [](int) {};
    std::function<void()> onOverview = [] {};

    // diff: dv for this pane's folder.
    std::function<void()> onDiffView = [] {};
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

    // What a long press on the bottom bar opens.
    void openMenu();

    // ⌨: the keyboard, typing into the pane, or into the composer when it is on.
    void raiseKeyboard();
    void switchToComposer();
    void showKeyRowSecondPage();
    void composeAndSend(const std::string& text);

    void resized() override;
    void safeAreaInsetsChanged() override;

    // Shows another pane in place of the one on screen.
    void showPane(const term::TerminalConfig& config, const std::string& paneId);

    term::TerminalView& getTerminal();

    PaneScreenActions actions;

private:
    void layOutParts();
    void returnToLiveBottom();
    void refreshWhatChangesOverTime();
    void showScrolledBackState(term::ScrolledBackState state);
    void closeMenu();
    std::vector<MenuAction> listMenuActions();
    eacp::Graphics::Insets calculateInsetsAroundParts() const;
    bool isLive() const;
    bool isKeyboardUp() const;
    void watchPaneOutput();
    void showClaudePrompt(std::optional<PermissionPrompt> prompt);
    void answerPrompt(const PermissionPrompt& drawnFor, int optionNumber);
    bool isClaudePane() const;
    float calculatePromptButtonsHeight() const;
    void showLinkStateOnPane();

    term::Theme theme;
    PaneDescription pane;
    LinkStatus link;
    float softwareKeyboardCoveredHeight = 0.0f;

    std::unique_ptr<term::TerminalView> terminal;
    PaneTouchSurface touchSurface {[this]() -> term::TerminalView& { return *terminal; }};

    eacp::UI::ComponentHost topLineHost;
    TopLine topLine {theme};

    eacp::UI::ComponentHost bottomBarHost;
    BottomBar bottomBar {theme};

    eacp::UI::ComponentHost pillHost;
    NewLinesPill pill {theme};

    eacp::UI::ComponentHost promptHost;
    PromptButtons promptButtons {theme};

    std::shared_ptr<OutputArrivedFlag> outputArrived = std::make_shared<OutputArrivedFlag>();
    std::chrono::steady_clock::time_point lastOutputAt;

    eacp::UI::ComponentHost menuHost;
    ActionMenu menu {theme};

    TypingControls typing {theme, [this]() -> term::TerminalView& { return *terminal; }};

    term::AppConfig pasteConfig;
    term::PasteConfirmation pasteConfirmation {pasteConfig};

    eacp::Threads::Timer refreshTimer;
    eacp::Threads::Timer outputWatchTimer;
};
} // namespace cowterm::mobile
