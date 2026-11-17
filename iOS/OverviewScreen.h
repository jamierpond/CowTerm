#pragma once

#include "OverviewLayout.h"

#include "Emulator/TermTypes.h"
#include "TerminalView/ScrollMomentum.h"

#include <eacp/UI/UI.h>

#include <functional>
#include <optional>

// The phone's overview (docs/iOSDesign.md, "The overview"): waiting sessions
// as cards and every session as a row, anchored at the bottom by the thumb,
// with the search field under the list. Drawn by eacp UI components like the
// pane screen's chrome.
namespace cowterm::mobile
{
using OpenSession = std::function<void(const term::overview::OverviewRow&)>;

// The scrolling list. A finger drags it and it coasts after the finger lifts,
// with the terminal's momentum; a tap opens the session under it.
class OverviewList final : public eacp::UI::Component
{
public:
    OverviewList();

    void setItems(std::vector<OverviewItem> itemsToShow);

    void paint(eacp::UI::Graphics& g) override;
    void mouseDown(const eacp::UI::MouseEvent& event) override;
    void mouseDrag(const eacp::UI::MouseEvent& event) override;
    void mouseUp(const eacp::UI::MouseEvent& event) override;
    void mouseCancel(const eacp::UI::MouseEvent& event) override;
    bool mouseWheelMove(const eacp::UI::MouseEvent& event) override;
    bool advanceAnimation(double seconds) override;

    OpenSession onOpenSession = [](const term::overview::OverviewRow&) {};

private:
    std::vector<eacp::UI::Rect> layOutItems() const;
    bool scrollByAndCheckIfStoppedAtEdge(float pixels);
    void openItemAt(eacp::UI::Point position);
    bool isPressed(const OverviewItem& item) const;
    void paintItem(eacp::UI::Graphics& g, const OverviewItem& item, eacp::UI::Rect bounds);
    void showPressAt(eacp::UI::Point position);
    void clearPress();
    void paintUnreachableHost(eacp::UI::Graphics& g,
                              const UnreachableHost& host,
                              eacp::UI::Rect bounds);

    std::vector<OverviewItem> items;
    float scrollOffset = 0.0f;
    float lastFingerY = 0.0f;
    bool isDragging = false;

    // A press that stops a coasting list opens nothing, as on an iOS list.
    bool pressStoppedACoast = false;
    std::optional<std::size_t> pressedItemIndex;
    term::ScrollMomentum momentum;
    term::TouchVelocityTracker fingerVelocity;
};

// The list, the search field under it and, when no machine answers, the
// banner in place of the list.
class OverviewPanel final : public eacp::UI::Component
{
public:
    explicit OverviewPanel(const term::Theme& themeToUse);

    void setInputs(OverviewInputs inputsToShow);

    // The screen's safe area and how much of it the keyboard covers, so the
    // search field can sit on top of the keyboard.
    void setScreenInsets(eacp::Graphics::Insets safeAreaToUse, float keyboardCoveredHeightToUse);
    void search(const std::string& query);

    // An empty field, the whole list, and the keyboard down.
    void clearSearch();

    const OverviewContent& getContent() const { return content; }
    const std::string& getQuery() const { return inputs.query; }

    void paint(eacp::UI::Graphics& g) override;
    void resized() override;

    OverviewList list;

private:
    void showSearchResults(const std::string& query);
    void openBestMatch();

    term::Theme theme;
    OverviewInputs inputs;
    OverviewContent content;
    eacp::UI::TextEditor searchField;
    eacp::UI::Button hideKeyboardButton {"hide"};
    eacp::Graphics::Insets safeArea;
    float keyboardCoveredHeight = 0.0f;
};

class OverviewScreen final : public eacp::Graphics::View
{
public:
    OverviewScreen();

    // hostCount is how many machines were asked, so the screen can tell
    // "one machine is down" from "nothing answers".
    void setInputs(OverviewInputs inputs);

    // From WindowEvents::onSoftwareKeyboardChanged.
    void setSoftwareKeyboardCoveredHeight(float coveredHeight);

    // Types query into the search field with the keyboard up, as a finger would.
    void search(const std::string& query);

    // Coming back to the list always shows all of it: the search is emptied.
    void clearSearch();

    void resized() override;
    void safeAreaInsetsChanged() override;

    OpenSession onOpenSession = [](const term::overview::OverviewRow&) {};

private:
    term::Theme theme;
    eacp::UI::ComponentHost host;
    OverviewPanel panel {theme};
    float keyboardCoveredHeight = 0.0f;
};
} // namespace cowterm::mobile
