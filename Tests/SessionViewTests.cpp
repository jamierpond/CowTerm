#include "SessionView.h"

#include <eacp/Core/Threads/EventLoop.h>

#include <NanoTest/NanoTest.h>

// The split tree, driven through real panes. Closing a pane collapses its
// parent split into the surviving sibling; these check that the active-pane
// marker survives that surgery.

using namespace nano;
using namespace eacp;
using namespace term;

namespace term
{
// The app's version (DaemonClient.cpp) wires the daemon-backed shell factory.
// Tests run in-process shells instead, so they never touch a live daemon.
TerminalConfig terminalConfig(const AppConfig&, const std::string&, const std::string&)
{
    return {};
}
} // namespace term

namespace
{
// Two panes side by side, the first active, as a restore leaves them.
std::vector<SavedPane> twoPaneSplit()
{
    auto root = SavedPane {};
    root.split = true;
    root.horizontal = true;
    root.first = 1;
    root.second = 2;

    return {root, SavedPane {.cwd = "/tmp"}, SavedPane {.cwd = "/tmp"}};
}
} // namespace

auto tClosingTheOtherPaneKeepsTheActiveOne =
    test("SessionView/closingAnInactivePaneKeepsTheActiveOne") = []
{
    // The crash: when the pane that closes is not the active one, its active
    // sibling's view moves up into the parent node and the sibling node dies.
    // `active` kept pointing at the sibling, so onActivePaneChanged saw a
    // leaf with no view and focusActive() called focus() on null.
    auto config = AppConfig {};
    auto view = SessionView {config, "/tmp"};
    auto panes = std::vector<TerminalView*> {};

    view.onPaneCreated = [&](TerminalView& pane) { panes.push_back(&pane); };
    view.restore(twoPaneSplit());
    view.setBounds({0.f, 0.f, 600.f, 300.f});

    check(panes.size() == 2);
    check(view.activePane() == panes[0]);

    auto* survivor = panes[0];
    auto activeWhenChanged = (TerminalView*) nullptr;
    auto changed = false;

    view.onActivePaneChanged = [&]
    {
        changed = true;
        activeWhenChanged = view.activePane();
    };

    panes[1]->terminateShell();

    Threads::runEventLoopUntil([&] { return changed; }, Time::MS {8000});

    check(changed, "the inactive pane's shell exit has to reach the tree");
    check(activeWhenChanged == survivor,
          "the active pane is still active while the tree reports the change");
    check(view.activePane() == survivor, "and stays active afterwards");
    check(view.paneCount() == 1);

    view.focusActive();
};

auto tClosingAPaneByName = test("SessionView/closePaneRemovesThatPaneOnly") = []
{
    // What Rudder's close does: one named pane goes, now, and the active
    // one is left alone.
    auto config = AppConfig {};
    auto view = SessionView {config, "/tmp"};
    auto panes = std::vector<TerminalView*> {};

    view.onPaneCreated = [&](TerminalView& pane) { panes.push_back(&pane); };
    view.restore(twoPaneSplit());
    view.setBounds({0.f, 0.f, 600.f, 300.f});

    check(panes.size() == 2);

    auto* survivor = panes[0];
    view.closePane(panes[1]);

    check(view.paneCount() == 1);
    check(view.activePane() == survivor);
    check(view.panes() == std::vector<TerminalView*> {survivor});

    // A pane that's already gone is nothing to close.
    view.closePane(panes[1]);
    check(view.paneCount() == 1);

    // The shell exit the close caused lands later and finds nothing to do.
    Threads::runEventLoopFor(Time::MS {200});
    check(view.paneCount() == 1);
};

namespace
{
constexpr auto viewWidth = 600.f;
constexpr auto viewHeight = 300.f;

// A left pane beside a right column of two stacked panes.
std::vector<SavedPane> leftPaneBesideStackedPair()
{
    auto root = SavedPane {};
    root.split = true;
    root.horizontal = true;
    root.first = 1;
    root.second = 2;

    auto column = SavedPane {};
    column.split = true;
    column.horizontal = false;
    column.first = 3;
    column.second = 4;

    return {root,
            SavedPane {.cwd = "/tmp"},
            column,
            SavedPane {.cwd = "/tmp"},
            SavedPane {.cwd = "/tmp"}};
}

struct ThreePaneView
{
    AppConfig config;
    SessionView view {config, "/tmp"};
    std::vector<TerminalView*> panes;

    ThreePaneView()
    {
        view.onPaneCreated = [this](TerminalView& pane) { panes.push_back(&pane); };
        view.setBounds({0.f, 0.f, viewWidth, viewHeight});
        view.restore(leftPaneBesideStackedPair());
    }

    TerminalView* left() const { return panes[0]; }
    TerminalView* topRight() const { return panes[1]; }
    TerminalView* bottomRight() const { return panes[2]; }
};

} // namespace

auto tFocusDirectionWalksTheTree = test("SessionView/focusDirectionMovesToTheNearestPane") = []
{
    auto fixture = ThreePaneView {};
    auto& view = fixture.view;

    check(fixture.panes.size() == 3);
    check(view.activePane() == fixture.left());

    view.focusDirection('h');
    check(view.activePane() == fixture.left(), "nothing lies left of the left pane");

    view.focusDirection('l');
    check(view.activePane() == fixture.topRight(), "ties go to the first leaf");

    view.focusDirection('j');
    check(view.activePane() == fixture.bottomRight());

    view.focusDirection('j');
    check(view.activePane() == fixture.bottomRight(), "nothing lies below the bottom");

    view.focusDirection('k');
    check(view.activePane() == fixture.topRight());

    view.focusDirection('h');
    check(view.activePane() == fixture.left());

    view.focusDirection('x');
    check(view.activePane() == fixture.left(), "an unknown direction moves nowhere");
};

auto tClosingTheActivePaneFocusesTheSiblingSubtree =
    test("SessionView/closingTheActivePaneFocusesTheFirstLeafOfWhatRemains") = []
{
    auto fixture = ThreePaneView {};
    auto& view = fixture.view;

    view.closePane(fixture.left());

    check(view.paneCount() == 2);
    check(view.activePane() == fixture.topRight());

    const auto saved = view.snapshot();
    check(saved.size() == 3);
    check(saved[0].split && !saved[0].horizontal,
          "the root takes over the column's vertical split");

    view.closePane(fixture.topRight());
    check(view.paneCount() == 1);
    check(view.activePane() == fixture.bottomRight());

    auto emptied = false;
    view.onEmpty = [&] { emptied = true; };
    view.closePane(fixture.bottomRight());
    check(emptied);
    check(view.activePane() == nullptr);
    check(view.paneCount() == 0);
};

auto tClosingAnInactivePaneInsideASplit =
    test("SessionView/closingAnInactiveSiblingKeepsTheActivePaneInItsNewNode") = []
{
    auto fixture = ThreePaneView {};
    auto& view = fixture.view;

    view.focusPane(fixture.bottomRight());
    view.closePane(fixture.topRight());

    check(view.paneCount() == 2);
    check(view.activePane() == fixture.bottomRight());

    view.focusDirection('h');
    check(view.activePane() == fixture.left());
};
