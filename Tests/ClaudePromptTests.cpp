#include "ClaudePrompt.h"

#include <NanoTest/NanoTest.h>

// Claude Code's permission prompt read off a screen snapshot, and the rule
// that a tap drawn for one prompt never answers another. iOS/ClaudePrompt.cpp,
// built here on the Mac.

using namespace nano;
using namespace cowterm::mobile;

namespace
{
constexpr auto boxedPrompt =
    "\xe2\x95\xad\xe2\x94\x80\xe2\x94\x80\xe2\x95\xae\n"
    "\xe2\x94\x82 Bash command                                        \xe2\x94\x82\n"
    "\xe2\x94\x82   rm -rf build                                      \xe2\x94\x82\n"
    "\xe2\x94\x82   Remove the build folder                           \xe2\x94\x82\n"
    "\xe2\x94\x82 Do you want to proceed?                             \xe2\x94\x82\n"
    "\xe2\x94\x82 \xe2\x9d\xaf 1. Yes                                     \xe2\x94\x82\n"
    "\xe2\x94\x82   2. Yes, and don't ask again for rm commands       \xe2\x94\x82\n"
    "\xe2\x94\x82   3. No, and tell Claude what to do differently (esc)\xe2\x94\x82\n"
    "\xe2\x95\xb0\xe2\x94\x80\xe2\x94\x80\xe2\x95\xaf\n";

constexpr auto samePromptOtherCommand =
    " Bash command\n"
    "   git push --force\n"
    "   Push to origin\n"
    " Do you want to proceed?\n"
    " \xe2\x9d\xaf 1. Yes\n"
    "   2. Yes, and don't ask again for rm commands\n"
    "   3. No, and tell Claude what to do differently (esc)\n";

// The boxed prompt on a screen 46 columns wide: the third option runs on into
// the next row, from its first column.
constexpr auto wrappedPrompt =
    " Bash command\n"
    "   rm -rf build\n"
    "   Remove the build folder\n"
    " Do you want to proceed?\n"
    " \xe2\x9d\xaf 1. Yes\n"
    "   2. Yes, and don't ask again for rm commands\n"
    "   3. No, and tell Claude what to do different\n"
    "ly (esc)\n"
    "\n";

constexpr auto promptWithABlankLineAboveTheQuestion =
    "Older output\n"
    " Bash command\n"
    "   rm -rf build\n"
    "   Remove the build folder\n"
    "\n"
    " Do you want to proceed?\n"
    " \xe2\x9d\xaf 1. Yes\n"
    "   2. No\n";

constexpr auto plainOutput = "$ ls\nDaemon  Lib  iOS\nAre you sure?\n$ \n";
constexpr auto firstOption = 1;
constexpr auto thirdOption = 3;
} // namespace

auto tBoxed = test("ClaudePrompt/readsTheOptionsOutOfClaudesBox") = []
{
    const auto prompt = findPermissionPrompt(boxedPrompt);

    check(prompt.has_value());
    check(prompt->question == "Do you want to proceed?");
    check(prompt->options.size() == 3);
    check(prompt->options[0].number == 1);
    check(prompt->options[0].label == "Yes");
    check(prompt->options[2].label == "No, and tell Claude what to do differently (esc)");
};

auto tNoPrompt = test("ClaudePrompt/aQuestionWithoutOptionsIsNoPrompt") = []
{
    check(!findPermissionPrompt(plainOutput).has_value());
    check(!findPermissionPrompt("").has_value());
};

auto tTapLands = test("ClaudePrompt/aTapOnTheShownPromptSendsItsNumber") = []
{
    const auto drawnFor = *findPermissionPrompt(boxedPrompt);
    const auto onScreen = findPermissionPrompt(boxedPrompt);

    check(chooseKeysForTappedOption(drawnFor, thirdOption, onScreen) == "3");
};

auto tTapForAnotherCommand = test("ClaudePrompt/aLateTapOnAPromptForAnotherCommandIsDropped") = []
{
    const auto drawnFor = *findPermissionPrompt(boxedPrompt);
    const auto onScreen = findPermissionPrompt(samePromptOtherCommand);

    check(onScreen.has_value());
    check(!chooseKeysForTappedOption(drawnFor, firstOption, onScreen).has_value());
};

auto tTapAfterAnswered = test("ClaudePrompt/aTapAfterThePromptWentIsDropped") = []
{
    const auto drawnFor = *findPermissionPrompt(boxedPrompt);
    const auto onScreen = findPermissionPrompt(plainOutput);

    check(!chooseKeysForTappedOption(drawnFor, firstOption, onScreen).has_value());
};

auto tWrapped = test("ClaudePrompt/anOptionWrappedByTheScreenReadsWhole") = []
{
    const auto prompt = findPermissionPrompt(wrappedPrompt);

    check(prompt.has_value());
    check(prompt->options.size() == 3);
    check(prompt->options[2].label == "No, and tell Claude what to do differently (esc)");
};

auto tWrappedIsSamePrompt = test("ClaudePrompt/aTapSurvivesTheScreenRewrappingThePrompt") = []
{
    const auto drawnFor = *findPermissionPrompt(wrappedPrompt);
    const auto unwrapped = findPermissionPrompt(
        " Bash command\n"
        "   rm -rf build\n"
        "   Remove the build folder\n"
        " Do you want to proceed?\n"
        " \xe2\x9d\xaf 1. Yes\n"
        "   2. Yes, and don't ask again for rm commands\n"
        "   3. No, and tell Claude what to do differently (esc)\n");

    check(chooseKeysForTappedOption(drawnFor, firstOption, unwrapped) == "1");
};

auto tBlankLineCounts = test("ClaudePrompt/aBlankLineAboveTheQuestionCountsAsOneOfItsLines") = []
{
    const auto prompt = findPermissionPrompt(promptWithABlankLineAboveTheQuestion);

    check(prompt.has_value());
    check(prompt->identity
          == "Bash command\n"
             "rm -rf build\n"
             "Remove the build folder\n"
             "\n"
             "Do you want to proceed?\n"
             "1. Yes\n"
             "2. No\n");
};
