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
TerminalConfig terminalConfig(const AppConfig&)
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
