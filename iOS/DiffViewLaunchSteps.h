#pragma once

#include "Typing.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What --diff-view-steps= does on dv's screen, one step at a time, for the
// review loop a headless simulator cannot do by hand (scripts/check-ios-diff-view-review.sh).
// The steps are separated by ";", each "action" or "action:argument":
//   keyboard          the bar's keyboard button
//   tap:<text>        a tap on the last place <text> is on dv's screen
//   type:<text>       <text> typed into dv, one key at a time, as the keyboard types it
//   key:<name>        a key row key: esc, tab, ctrl, up, down, left or right
namespace cowterm::mobile
{
enum class DiffViewLaunchAction
{
    RaiseKeyboard,
    TapText,
    TypeText,
    PressKeyRowKey
};

struct DiffViewLaunchStep
{
    DiffViewLaunchAction action = DiffViewLaunchAction::RaiseKeyboard;
    std::string text;
    KeyRowKey key = KeyRowKey::Escape;
};

// Steps it cannot read are left out.
std::vector<DiffViewLaunchStep> parseDiffViewLaunchSteps(std::string_view steps);

struct ScreenTextPosition
{
    int row = 0;
    int column = 0;
};

// The cell in the middle of the last copy of text in captureVisibleText's
// output, counting columns in characters.
std::optional<ScreenTextPosition> findLastTextOnScreen(const std::string& screenText,
                                                       const std::string& text);

// Each character of UTF-8 text on its own, as the keyboard types them.
std::vector<std::string> splitIntoCharacters(std::string_view text);
} // namespace cowterm::mobile
