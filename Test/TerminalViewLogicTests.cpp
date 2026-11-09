#include <CowTermTerminal/CellText.h>
#include <CowTermTerminal/CopyMode.h>
#include <CowTermTerminal/GridPainter.h>
#include <CowTermTerminal/KeyEncoding.h>
#include <CowTermTerminal/MacKeyCodes.h>

#include <CowTermCore/TermParser.h>

#include <NanoTest/NanoTest.h>

#include <string>

// The terminal widget's decisions with no view, GPU or shell behind them: key
// and mouse encodings, copy-mode commands and motions, text extraction and the
// colours and runs the painter draws.

using namespace nano;
using namespace term;
using eacp::Graphics::KeyEvent;
using eacp::Graphics::ModifierKeys;
namespace KeyCode = eacp::Graphics::KeyCode;

namespace
{
constexpr auto screenColumns = 20;
constexpr auto screenRows = 4;
constexpr std::uint16_t noKeyCode = 0xffff;

struct ParsedScreen
{
    explicit ParsedScreen(const std::string& output)
    {
        parser.feed(output);
    }

    Theme theme;
    TermScreen screen {screenColumns, screenRows, theme};
    TermParser parser {screen, theme};
};

KeyEvent typedKey(const std::string& text, ModifierKeys mods = {})
{
    auto event = KeyEvent {};
    event.characters = text;
    event.charactersIgnoringModifiers = text;
    event.keyCode = noKeyCode;
    event.modifiers = mods;
    return event;
}

KeyEvent keyWithCode(std::uint16_t keyCode, ModifierKeys mods = {})
{
    auto event = KeyEvent {};
    event.keyCode = keyCode;
    event.modifiers = mods;
    return event;
}

ModifierKeys controlShiftHeld()
{
    auto mods = ModifierKeys {};
    mods.control = true;
    mods.shift = true;
    return mods;
}

ModifierKeys controlHeld()
{
    auto mods = ModifierKeys {};
    mods.control = true;
    return mods;
}

ModifierKeys commandHeld()
{
    auto mods = ModifierKeys {};
    mods.command = true;
    return mods;
}

bool isMove(const std::optional<CopyModeCommand>& command, CopyModeMotion motion)
{
    return command && command->action == CopyModeAction::Move && command->motion == motion;
}

bool isAction(const std::optional<CopyModeCommand>& command, CopyModeAction action)
{
    return command && command->action == action;
}

bool isAt(CellRef cell, long row, int col)
{
    return cell.row == row && cell.col == col;
}
} // namespace

// ---- key encoding -------------------------------------------------------------

auto tShiftPagingJumps = test("ViewLogic/shiftPagingIsAViewportJump") = []
{
    auto event = keyWithCode(MacKey::PageUp);
    event.modifiers.shift = true;

    const auto response = encodeSpecialKey(event, false);
    check(response && std::get<ViewportJump>(*response) == ViewportJump::PageTowardOldest);
};

auto tUnknownKeyIsNotSpecial = test("ViewLogic/anOrdinaryKeyIsNotSpecial") = []
{
    check(!encodeSpecialKey(typedKey("a"), false));
    check(!encodeSpecialKey(typedKey("a"), true));
};

auto tTypedKeyWithNothing = test("ViewLogic/aKeyWithNoCharactersSendsNothing") = []
{
    check(!encodeTypedKey(keyWithCode(noKeyCode)));
};

auto tCommandShortcutMapping = test("ViewLogic/commandShortcuts") = []
{
    check(findCommandShortcut(typedKey("c", commandHeld())) == EditingShortcut::Copy);
    check(findCommandShortcut(typedKey("v", commandHeld())) == EditingShortcut::Paste);
    check(findCommandShortcut(typedKey("k", commandHeld())) == EditingShortcut::ClearScreen);
    check(findCommandShortcut(typedKey("q", commandHeld())) == EditingShortcut::Swallow);
    check(!findCommandShortcut(typedKey("c")));
};

auto tControlShiftClipboard = test("ViewLogic/controlShiftCopyAndPaste") = []
{
    check(findControlShiftClipboardShortcut(typedKey("C", controlShiftHeld()))
          == EditingShortcut::Copy);
    check(findControlShiftClipboardShortcut(typedKey("V", controlShiftHeld()))
          == EditingShortcut::Paste);
    check(!findControlShiftClipboardShortcut(typedKey("X", controlShiftHeld())));
    check(!findControlShiftClipboardShortcut(typedKey("c", controlHeld())),
          "plain Ctrl+C is SIGINT");

    auto withAlt = typedKey("C", controlShiftHeld());
    withAlt.modifiers.alt = true;
    check(!findControlShiftClipboardShortcut(withAlt));
};

auto tPasteNormalisesNewlines = test("ViewLogic/pasteTurnsNewlinesIntoEnter") = []
{
    check(preparePastedText("a\r\nb\nc\rd", false) == "a\rb\rc\rd");
    check(preparePastedText("trailing\r", false) == "trailing\r");
};

auto tBracketedPaste = test("ViewLogic/bracketedPasteWrapsTheText") = []
{
    check(preparePastedText("ls\n", true) == "\033[200~ls\r\033[201~");
};

auto tSgrEncoding = test("ViewLogic/sgrMouseReportEncoding") = []
{
    check(encodeSgrMouseReport({0, true, false, 3, 2}) == "\033[<0;3;2M");
    check(encodeSgrMouseReport({2, false, false, 1, 1}) == "\033[<2;1;1m");
    check(encodeSgrMouseReport({0, true, true, 10, 4}) == "\033[<32;10;4M");
};

auto tWheelArrows = test("ViewLogic/wheelArrowKeysAreCapped") = []
{
    const auto up = encodeWheelAsArrowKeys(3, false);
    check(up.key == "\033[A" && up.count == 3);

    const auto down = encodeWheelAsArrowKeys(-500, true);
    check(down.key == "\033OB" && down.count == 40);
};

// ---- copy mode ----------------------------------------------------------------

auto tCopyModeKeysMapToCommands = test("ViewLogic/copyModeKeyBindings") = []
{
    check(!findCopyModeCommand(typedKey("y", commandHeld())), "Cmd chords pass through");

    check(isAction(findCopyModeCommand(keyWithCode(KeyCode::Escape)),
                   CopyModeAction::CancelSelectionOrExit));
    check(isAction(findCopyModeCommand(typedKey("q")), CopyModeAction::Exit));
    check(isAction(findCopyModeCommand(typedKey("c", controlHeld())), CopyModeAction::Exit));
    check(isAction(findCopyModeCommand(typedKey("y")), CopyModeAction::YankAndExit));
    check(isAction(findCopyModeCommand(keyWithCode(KeyCode::Return)),
                   CopyModeAction::YankAndExit));
    check(isAction(findCopyModeCommand(typedKey("v")),
                   CopyModeAction::BeginCharacterSelection));
    check(isAction(findCopyModeCommand(typedKey(" ")),
                   CopyModeAction::BeginCharacterSelection));
    check(isAction(findCopyModeCommand(typedKey("V")), CopyModeAction::BeginLineSelection));
    check(isAction(findCopyModeCommand(typedKey("x")), CopyModeAction::Nothing));
};

auto tCopyModeMotionKeys = test("ViewLogic/copyModeMotionKeys") = []
{
    check(isMove(findCopyModeCommand(typedKey("h")), CopyModeMotion::Left));
    check(isMove(findCopyModeCommand(keyWithCode(KeyCode::DownArrow)), CopyModeMotion::Down));
    check(isMove(findCopyModeCommand(keyWithCode(MacKey::End)), CopyModeMotion::LineEnd));
    check(isMove(findCopyModeCommand(typedKey("G")), CopyModeMotion::Bottom));
    check(isMove(findCopyModeCommand(typedKey("b")), CopyModeMotion::PreviousWord));
    check(isMove(findCopyModeCommand(typedKey("b", controlHeld())), CopyModeMotion::PageUp),
          "Ctrl+B pages instead of moving a word");
    check(isMove(findCopyModeCommand(typedKey("u", controlHeld())),
                 CopyModeMotion::HalfPageUp));
    check(isAction(findCopyModeCommand(typedKey("x", controlHeld())),
                   CopyModeAction::Nothing));

    auto pageDownWithLetter = typedKey("h");
    pageDownWithLetter.keyCode = MacKey::PageDown;
    check(isMove(findCopyModeCommand(pageDownWithLetter), CopyModeMotion::PageDown),
          "the paging keys win over the letter bindings");
};

auto tCopyModeMotionTargets = test("ViewLogic/copyModeMotionTargets") = []
{
    const auto parsed = ParsedScreen {"aa bb cc"};
    const auto& screen = parsed.screen;
    const auto cursor = CellRef {2, 5};

    check(isAt(findCopyModeMotionTarget(CopyModeMotion::HalfPageUp, cursor, screen), 0, 5));
    check(isAt(findCopyModeMotionTarget(CopyModeMotion::PageDown, cursor, screen), 5, 5));
    check(isAt(findCopyModeMotionTarget(CopyModeMotion::Left, cursor, screen), 2, 4));
    check(isAt(findCopyModeMotionTarget(CopyModeMotion::Top, cursor, screen), 0, 0));
    check(isAt(findCopyModeMotionTarget(CopyModeMotion::Bottom, cursor, screen), 3, 0));
    check(isAt(findCopyModeMotionTarget(CopyModeMotion::LineEnd, {0, 0}, screen), 0, 7));
    check(isAt(findCopyModeMotionTarget(CopyModeMotion::LineEnd, {1, 4}, screen), 1, 0),
          "an empty line ends at column zero");
};

auto tWordMotions = test("ViewLogic/wordMotionsWrapAcrossRows") = []
{
    const auto parsed = ParsedScreen {"aa bb\r\n  cc"};
    const auto& screen = parsed.screen;

    check(isAt(findNextWordStart({0, 0}, screen), 0, 3));
    check(isAt(findNextWordStart({0, 3}, screen), 1, 2));
    check(isAt(findPreviousWordStart({1, 2}, screen), 0, 3));
    check(isAt(findPreviousWordStart({0, 4}, screen), 0, 3));
    check(isAt(findPreviousWordStart({0, 0}, screen), 0, 0));
    check(isAt(findNextWordStart({3, screenColumns - 1}, screen), 3, screenColumns - 1),
          "the last cell has nowhere to go");
};

// ---- cell text ----------------------------------------------------------------

auto tTextBetween = test("ViewLogic/selectedTextIsTrimmedPerRow") = []
{
    const auto parsed = ParsedScreen {"aa bb   \r\ncc dd"};

    check(extractTextBetween(parsed.screen, {0, 3}, {1, 1}) == "bb\ncc");
    check(extractTextBetween(parsed.screen, {0, 0}, {0, 19}) == "aa bb");
    check(extractTextBetween(parsed.screen, {1, 3}, {1, 4}) == "dd");
};

auto tTextBetweenWide = test("ViewLogic/wideCharactersAppearOnce") = []
{
    const auto parsed = ParsedScreen {"\xe4\xb8\xad!"};
    check(extractTextBetween(parsed.screen, {0, 0}, {0, 5}) == "\xe4\xb8\xad!");
};

auto tVisibleRowText = test("ViewLogic/visibleRowTextStopsAtTheColumns") = []
{
    const auto parsed = ParsedScreen {"abcdef  "};
    check(extractVisibleRowText(parsed.screen, parsed.screen.lineAt(0, 0), 4) == "abcd");
    check(extractVisibleRowText(parsed.screen, parsed.screen.lineAt(0, 0), screenColumns) == "abcdef");
};

auto tWordAround = test("ViewLogic/wordAroundAColumn") = []
{
    const auto parsed = ParsedScreen {"aa bbb cc"};
    const auto& line = parsed.screen.lineAt(0, 0);

    const auto word = findWordAround(line, 4);
    check(word.isOnWord && word.firstColumn == 3 && word.lastColumn == 5);

    const auto gap = findWordAround(line, 2);
    check(!gap.isOnWord);

    const auto first = findWordAround(line, 0);
    check(first.firstColumn == 0 && first.lastColumn == 1);
};

auto tInkLength = test("ViewLogic/inkLengthIgnoresTrailingBlanks") = []
{
    const auto parsed = ParsedScreen {"ab cd   "};
    check(measureInkLength(parsed.screen.lineAt(0, 0)) == 5);
    check(measureInkLength(parsed.screen.lineAt(1, 0)) == 0);
};

// ---- painting decisions -------------------------------------------------------

auto tCursorStyles = test("ViewLogic/cursorShapes") = []
{
    check(findCursorStyle(0) == CursorStyle::Block);
    check(findCursorStyle(2) == CursorStyle::Block);
    check(findCursorStyle(3) == CursorStyle::Underline);
    check(findCursorStyle(4) == CursorStyle::Underline);
    check(findCursorStyle(5) == CursorStyle::Bar);
    check(findCursorStyle(6) == CursorStyle::Bar);

    check(isBlinkingCursorShape(0));
    check(isBlinkingCursorShape(1));
    check(!isBlinkingCursorShape(2));
    check(isBlinkingCursorShape(5));
    check(!isBlinkingCursorShape(6));
};

auto tCellColours = test("ViewLogic/inverseAndSelectionColours") = []
{
    const auto theme = Theme {};
    auto cell = ResolvedCell {U'a', {}, 0x111111, 0x222222};

    const auto plain = findCellColours(cell, false, theme);
    check(plain.foreground == 0x111111 && plain.background == 0x222222);

    cell.attrs |= Attr::Inverse;
    const auto inverse = findCellColours(cell, false, theme);
    check(inverse.foreground == 0x222222 && inverse.background == 0x111111);

    const auto selected = findCellColours(cell, true, theme);
    check(selected.foreground == 0x222222 && selected.background == theme.selection);
};

auto tBackgroundRuns = test("ViewLogic/backgroundRunsMergeAndSkipTheDefault") = []
{
    const auto parsed = ParsedScreen {"\033[41mab\033[42mc\033[0m d\033[41me"};
    const auto& line = parsed.screen.lineAt(0, 0);
    const auto row = PaintedRow {parsed.screen.resolveLine(line), 0, 0.f, screenColumns};
    const auto selection = SelectionRange {{0, 9}, {0, 10}, true};

    const auto runs = findBackgroundRuns(row, selection, parsed.theme);

    check(runs.size() == 4);
    check(runs[0].firstColumn == 0 && runs[0].endColumn == 2);
    check(runs[0].colour == parsed.theme.ansi[1]);
    check(runs[1].firstColumn == 2 && runs[1].endColumn == 3);
    check(runs[2].firstColumn == 5 && runs[2].endColumn == 6);
    check(runs[3].firstColumn == 9 && runs[3].endColumn == 11);
    check(runs[3].colour == parsed.theme.selection);
};

auto tSelectionContains = test("ViewLogic/selectionRangeIsInclusive") = []
{
    const auto range = SelectionRange {{1, 5}, {2, 3}, true};
    check(range.contains({1, 5}));
    check(range.contains({1, 19}));
    check(range.contains({2, 3}));
    check(!range.contains({1, 4}));
    check(!range.contains({2, 4}));

    auto inactive = range;
    inactive.active = false;
    check(!inactive.contains({1, 6}));
};
