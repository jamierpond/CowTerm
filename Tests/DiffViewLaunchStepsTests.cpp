#include "DiffViewLaunchSteps.h"

#include <NanoTest/NanoTest.h>

// The steps --diff-view-steps= plays on dv's screen, and how a step finds the
// text it taps. iOS/DiffViewLaunchSteps.cpp, built here on the Mac.

using namespace nano;
using namespace cowterm::mobile;

namespace
{
constexpr auto reviewLoop =
    "keyboard;tap:beta changed;tap:Comment;type:Looks right;key:ctrl;type:s;compose:From the composer";

// dv at 45 columns with a line selected, as tmux showed it on macprorack.
constexpr auto diffViewScreen = " beta.txt\n"
                                "\xe2\x96\x8c" "2+ beta changed\n"
                                " Tap a line, then Comment\n"
                                "\xe2\x95\xad\xe2\x94\x80\xe2\x95\xae \xe2\x95\xad\xe2\x94\x80\xe2\x95\xae\n"
                                "\xe2\x94\x82 Comment \xe2\x94\x82 \xe2\x94\x82 Submit (0) \xe2\x94\x82\n";
} // namespace

auto tParseLoop = test("DiffViewLaunchSteps/readsEachStepInOrder") = []
{
    const auto steps = parseDiffViewLaunchSteps(reviewLoop);

    check(steps.size() == 7);
    check(steps[0].action == DiffViewLaunchAction::RaiseKeyboard);
    check(steps[1].action == DiffViewLaunchAction::TapText);
    check(steps[1].text == "beta changed");
    check(steps[3].action == DiffViewLaunchAction::TypeText);
    check(steps[3].text == "Looks right");
    check(steps[4].action == DiffViewLaunchAction::PressKeyRowKey);
    check(steps[4].key == KeyRowKey::Control);
    check(steps[6].action == DiffViewLaunchAction::ComposeAndSend);
    check(steps[6].text == "From the composer");
};

auto tParseUnknown = test("DiffViewLaunchSteps/leavesOutStepsItCannotRead") = []
{
    const auto steps = parseDiffViewLaunchSteps("wave;key:hyper;key:esc;");

    check(steps.size() == 1);
    check(steps[0].key == KeyRowKey::Escape);
};

auto tFindButton = test("DiffViewLaunchSteps/findsTheButtonNotTheHintAboveIt") = []
{
    const auto position = findLastTextOnScreen(diffViewScreen, "Comment");

    check(position.has_value());
    check(position->row == 4);
    check(position->column == 5);
};

auto tFindAfterWideBytes = test("DiffViewLaunchSteps/countsColumnsInCharactersNotBytes") = []
{
    const auto position = findLastTextOnScreen(diffViewScreen, "beta changed");

    check(position.has_value());
    check(position->row == 1);
    check(position->column == 10);
};

auto tFindMissing = test("DiffViewLaunchSteps/findsNothingThatIsNotOnScreen") = []
{
    check(!findLastTextOnScreen(diffViewScreen, "Save").has_value());
};

auto tSplit = test("DiffViewLaunchSteps/splitsTextIntoWholeCharacters") = []
{
    const auto characters = splitIntoCharacters("a\xc2\xb7" "b");

    check(characters.size() == 3);
    check(characters[1] == "\xc2\xb7");
};
