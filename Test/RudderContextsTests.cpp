#include "RudderContexts.h"

#include <NanoTest/NanoTest.h>

// The context list CowTerm publishes to Rudder: one row per pane, then the
// ctrl+a f places nothing is open in yet.

using namespace nano;
using namespace term;

namespace
{
const auto home = std::string {"/Users/me"};

RudderTab twoPaneTab()
{
    return {"CowTerm",
            "/Users/me/projects/CowTerm",
            {{"a1", "", "zsh", "/Users/me/projects/CowTerm"},
             {"a2", "", "nvim", "/Users/me/projects/CowTerm/Lib"}}};
}
} // namespace

auto tOneContextPerPane = test("RudderContexts/oneContextPerPane") = []
{
    const auto contexts =
        rudderContexts({{"home", "/Users/me", {{"h1", "", "zsh", "/Users/me"}}},
                        twoPaneTab()},
                       {},
                       home);

    check(contexts.size() == 3);
    check(contexts[0].id == "pane:h1");
    check(contexts[1].id == "pane:a1");
    check(contexts[2].id == "pane:a2");
    check(contexts[1].subtitle == "Tab 2 · pane 1 · CowTerm");
    check(contexts[2].subtitle == "Tab 2 · pane 2 · CowTerm");
    check(contexts[0].open && contexts[1].open, "panes preview freely");
};

auto tTitleCarriesCommandAndCwd = test("RudderContexts/titleCarriesCommandAndCwd") = []
{
    const auto contexts = rudderContexts({twoPaneTab()}, {}, home);

    check(contexts[0].title == "~/projects/CowTerm", "a shell at its prompt is its cwd");
    check(contexts[1].title == "nvim — ~/projects/CowTerm/Lib");
};

auto tTitleSequenceWins = test("RudderContexts/titleSequenceWins") = []
{
    auto tab = twoPaneTab();
    tab.panes[0].title = "~/projects/CowTerm";
    tab.panes[1].title = "nvim Main.cpp";

    const auto contexts = rudderContexts({tab}, {}, home);

    check(contexts[0].title == "~/projects/CowTerm", "no repeated cwd");
    check(contexts[1].title == "nvim Main.cpp — ~/projects/CowTerm/Lib",
          "no repeated command");
};

auto tCommandJoinsAnUnrelatedTitle = test("RudderContexts/commandJoinsTitle") = []
{
    auto tab = twoPaneTab();
    tab.panes[1].command = "htop";
    tab.panes[1].title = "load";

    check(rudderContexts({tab}, {}, home)[1].title == "htop · load — ~/projects/CowTerm/Lib");
};

auto tStatusGlyphIsDropped = test("RudderContexts/statusGlyphIsDropped") = []
{
    auto tab = twoPaneTab();
    tab.panes[1].command = "claude";
    tab.panes[1].title = "◐ Fix the build";

    auto spinning = rudderContexts({tab}, {}, home);
    tab.panes[1].title = "◑ Fix the build";

    check(spinning[1].title == "claude · Fix the build — ~/projects/CowTerm/Lib");
    check(spinning == rudderContexts({tab}, {}, home), "a spinner frame isn't a change");

    tab.panes[1].title = "~/x y";
    check(rudderContexts({tab}, {}, home)[1].title.starts_with("claude · ~/x y"));
};

auto tHomePrefixIsWholeComponent = test("RudderContexts/homePrefixIsWholeComponent") = []
{
    const auto contexts =
        rudderContexts({{"x", "x", {{"s", "", "zsh", "/Users/meg/src"}}}}, {}, home);

    check(contexts[0].title == "/Users/meg/src");
};

auto tPlacesAfterPanes = test("RudderContexts/placesAfterPanes") = []
{
    const auto contexts = rudderContexts(
        {twoPaneTab()},
        {"/Users/me/projects", "/Users/me/projects/tamber-web", "/Users/me/projects/CowTerm"},
        home);

    check(contexts.size() == 4, "the open tab's dir is covered by its panes");
    check(contexts[2].id == "place:/Users/me/projects");
    check(contexts[3].id == "place:/Users/me/projects/tamber-web");
    check(contexts[3].title == "tamber-web");
    check(contexts[3].subtitle == "~/projects/tamber-web");
    check(!contexts[3].open, "activating a place creates a session: no preview");
};

auto tPaneCwdCoversPlace = test("RudderContexts/paneCwdCoversPlace") = []
{
    const auto contexts =
        rudderContexts({twoPaneTab()}, {"/Users/me/projects/CowTerm/Lib"}, home);

    check(contexts.size() == 2);
};

auto tParsesIds = test("RudderContexts/parsesIds") = []
{
    auto pane = parseRudderContextId("pane:abc");
    auto place = parseRudderContextId("place:/Users/me/x");

    check(pane && pane->kind == RudderTarget::Kind::Pane && pane->value == "abc");
    check(place && place->kind == RudderTarget::Kind::Place
          && place->value == "/Users/me/x");
    check(!parseRudderContextId("pane:"));
    check(!parseRudderContextId("3"));
};
