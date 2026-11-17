#include "OverviewScreen.h"

#include <NanoTest/NanoTest.h>

// The phone overview's panel, built on the Mac: what coming back to the list
// shows.

using namespace nano;
using namespace cowterm::mobile;

namespace
{
constexpr auto screenWidth = 375.0f;
constexpr auto screenHeight = 812.0f;

OverviewInputs makeTwoSessions()
{
    auto cowterm = term::overview::OverviewRow {};
    cowterm.host = "jamie@tamby";
    cowterm.sessionName = "CowTerm";
    auto grazer = cowterm;
    grazer.sessionName = "Grazer";

    auto inputs = OverviewInputs {};
    inputs.sessions = {cowterm, grazer};
    inputs.hostCount = 1;
    return inputs;
}
} // namespace

auto tReturningClearsTheSearch = test("OverviewScreen/comingBackToTheListClearsTheSearch") = []
{
    auto panel = OverviewPanel {term::Theme {}};
    panel.setBounds({0.0f, 0.0f, screenWidth, screenHeight});
    panel.setInputs(makeTwoSessions());

    panel.search("graz");
    check(panel.getQuery() == "graz");
    check(panel.getContent().itemsFromTheBottom.size() == 1);

    panel.clearSearch();
    check(panel.getQuery().empty());
    check(panel.getContent().itemsFromTheBottom.size() == 2, "the whole list again");
};
