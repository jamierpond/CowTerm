#pragma once

#include "PaneScreenInputs.h"
#include "PaneScreenLayout.h"

#include "Emulator/TermTypes.h"

#include <eacp/UI/UI.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

// The pane screen's chrome, drawn on the GPU by eacp UI components like the
// Mac's overlays: the top line, the bottom bar, the "↓ n new" pill and the
// menu that lists every action.
namespace cowterm::mobile
{
// "tamby/Grazer" in the host's colour on the left, the link state on the right.
class TopLine final : public eacp::UI::Component
{
public:
    explicit TopLine(const term::Theme& themeToUse);

    void paint(eacp::UI::Graphics& g) override;

    PaneDescription pane;
    LinkStatus link;
    term::Rgb hostColour = 0;

private:
    term::Theme theme;
};

struct BottomBarActions
{
    std::function<void()> onPreviousSession = [] {};
    std::function<void(int paneIndex)> onChoosePane = [](int) {};
    std::function<void()> onKeyboard = [] {};
    std::function<void()> onGit = [] {};
    std::function<void()> onOverview = [] {};
    std::function<void()> onOpenMenu = [] {};
};

// ‹ previous session, the pane dots, ⌨, git and ☰. A tap presses a control,
// a sideways swipe moves between panes, a swipe up opens the overview and a
// long press opens the menu.
class BottomBar final : public eacp::UI::Component
{
public:
    explicit BottomBar(const term::Theme& themeToUse);
    ~BottomBar() override;

    void paint(eacp::UI::Graphics& g) override;
    void mouseDown(const eacp::UI::MouseEvent& event) override;
    void mouseDrag(const eacp::UI::MouseEvent& event) override;
    void mouseUp(const eacp::UI::MouseEvent& event) override;

    PaneDescription pane;
    BottomBarActions actions;

private:
    // The chip's width is measured when the bar paints, which is always before
    // a finger can land on it.
    BottomBarLayout calculateLayout() const;
    std::string describePreviousSession() const;
    void pressControlAt(eacp::UI::Point position);
    void waitForLongPress();
    bool isStillHeldSince(int generation) const;

    term::Theme theme;
    eacp::UI::Font font;
    float previousSessionWidth = 0.0f;
    bool isFingerDown = false;
    bool hasLongPressFired = false;
    int pressGeneration = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

class NewLinesPill final : public eacp::UI::Component
{
public:
    explicit NewLinesPill(const term::Theme& themeToUse);

    void paint(eacp::UI::Graphics& g) override;
    void mouseUp(const eacp::UI::MouseEvent& event) override;

    std::uint64_t newLineCount = 0;
    std::function<void()> onReturnToBottom = [] {};

private:
    term::Theme theme;
};

// One row of the menu: what it does, the gesture that does it too, and
// whether it is built yet. Rows that are not built are drawn greyed and do
// nothing.
struct MenuAction
{
    std::string name;
    std::string gesture;
    bool isBuilt = false;
    std::function<void()> perform = [] {};
};

// The long press menu: every action with its gesture beside it, so nothing is
// only a gesture. A tap on a row does it; a tap outside closes the menu.
class ActionMenu final : public eacp::UI::Component
{
public:
    explicit ActionMenu(const term::Theme& themeToUse);

    void paint(eacp::UI::Graphics& g) override;
    void mouseUp(const eacp::UI::MouseEvent& event) override;

    std::vector<MenuAction> actions;
    std::function<void()> onClose = [] {};

private:
    eacp::UI::Rect calculateCardBounds() const;
    float calculateRowHeight() const;

    term::Theme theme;
};
} // namespace cowterm::mobile
