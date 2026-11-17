#include <TerminalView/CellText.h>
#include <TerminalView/CopyMode.h>
#include <TerminalView/GridPainter.h>
#include <TerminalView/KeyEncoding.h>
#include <TerminalView/LinkDetection.h>
#include <TerminalView/MacKeyCodes.h>

#include <Emulator/TermParser.h>

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

auto tPasteStripsControls = test("ViewLogic/pasteTurnsControlBytesIntoSpaces") = []
{
    check(preparePastedText("a\033[201~b", true) == "\033[200~a [201~b\033[201~");
    check(preparePastedText("x\x03y\x15z\tw", false) == "x y z\tw");
};

auto tPasteSafety = test("ViewLogic/pasteIsUnsafeWhenItCouldRunACommand") = []
{
    check(isPasteSafe("echo hi", false));
    check(isPasteSafe("tab\tseparated", false));
    check(!isPasteSafe("rm -rf ~\n", false));
    check(!isPasteSafe("a\rb", false));
    check(isPasteSafe("two\nlines", true), "bracketed paste reaches the program as one block");
    check(!isPasteSafe("x\033[201~rm -rf ~\n", true), "an end marker escapes the bracket");
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

auto tWheelReports = test("ViewLogic/wheelMouseReportsAreCapped") = []
{
    const auto up = encodeWheelAsMouseReports(2);
    check(up.button == wheelUpMouseButton && up.count == 2);

    const auto down = encodeWheelAsMouseReports(-500);
    check(down.button == wheelDownMouseButton && down.count == 40);
};

auto tWheelDeltaToLines = test("ViewLogic/trackpadPointsBecomeLinesAndWheelLinesStay") = []
{
    constexpr auto pointsPerLine = 16.f;
    constexpr auto trackpadPoints = 40.f;
    constexpr auto wheelLines = 3.f;

    check(convertWheelDeltaToLines(trackpadPoints, true, pointsPerLine) == 2.5f);
    check(convertWheelDeltaToLines(wheelLines, false, pointsPerLine) == wheelLines);
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

auto tTextFromRightHalf = test("ViewLogic/aSelectionStartingOnAWideCharactersRightHalfTakesIt") = []
{
    const auto parsed = ParsedScreen {"\xe4\xb8\xad!"};
    constexpr auto rightHalf = CellRef {0, 1};
    constexpr auto exclamation = CellRef {0, 2};

    check(extractTextBetween(parsed.screen, rightHalf, exclamation) == "\xe4\xb8\xad!");
};

auto tTextAcrossSoftWrap = test("ViewLogic/copyJoinsSoftWrappedRowsWithoutANewline") = []
{
    const auto parsed = ParsedScreen {"0123456789abcdefghij klmn\r\nnext"};

    check(parsed.screen.lineAt(0, 0).wrapped);
    check(extractTextBetween(parsed.screen, {0, 0}, {2, 3}) == "0123456789abcdefghij klmn\nnext");
};

auto tSpaceAtTheWrapIsKept = test("ViewLogic/copyKeepsASpaceThatEndsAWrappedRow") = []
{
    const auto parsed = ParsedScreen {"0123456789abcdefghi word"};

    check(extractTextBetween(parsed.screen, {0, 0}, {1, 3}) == "0123456789abcdefghi word");
};

// ---- links --------------------------------------------------------------------

namespace
{
std::string findUrlTextAround(std::string_view text, std::size_t byteIndex)
{
    const auto range = findUrlAround(text, byteIndex);

    if (!range)
        return {};

    return std::string {text.substr(range->begin, range->end - range->begin)};
}
} // namespace

auto tUrlInText = test("Links/findsAUrlAroundTheIndex") = []
{
    constexpr auto text = std::string_view {"see https://a.test/x?q=1 now"};
    constexpr auto insideUrl = 10;
    constexpr auto onSee = 1;

    check(findUrlTextAround(text, insideUrl) == "https://a.test/x?q=1");
    check(findUrlTextAround(text, onSee).empty());
};

auto tUrlTrimsPunctuation = test("Links/urlLeavesOffTrailingPunctuationAndUnmatchedBrackets") = []
{
    constexpr auto insideUrl = 8;

    check(findUrlTextAround("(https://a.test/x).", insideUrl) == "https://a.test/x");
    check(findUrlTextAround("https://w.test/Rust_(game), ok", insideUrl)
          == "https://w.test/Rust_(game)");
    check(findUrlTextAround("'https://a.test/'", insideUrl) == "https://a.test/");
    check(findUrlTextAround("x-app://run", insideUrl).empty(), "only openable schemes");
    check(findUrlTextAround("https://", insideUrl - 1).empty(), "a bare scheme is no link");
};

auto tUrlAcrossSoftWrap = test("Links/urlRunsAcrossSoftWrappedRows") = []
{
    const auto parsed = ParsedScreen {"go https://a.test/long/path ok"};
    constexpr auto secondRow = 1L;
    constexpr auto lastUrlColumn = 6;

    const auto fromFirstRow = findLinkAt(parsed.screen, {0, 5});
    const auto fromSecondRow = findLinkAt(parsed.screen, {secondRow, 2});

    check(fromFirstRow && fromFirstRow->uri == "https://a.test/long/path");
    check(fromSecondRow && fromSecondRow->uri == "https://a.test/long/path");
    check(fromFirstRow->span.start == CellRef {0, 3});
    check(fromFirstRow->span.end == CellRef {secondRow, lastUrlColumn});
    check(!findLinkAt(parsed.screen, {secondRow, lastUrlColumn + 2}), "the word after it");
};

auto tUrlStopsAtHardNewline = test("Links/urlStopsAtARealNewline") = []
{
    const auto parsed = ParsedScreen {"https://a.test/\r\nmore"};
    const auto link = findLinkAt(parsed.screen, {0, 2});

    check(link && link->uri == "https://a.test/");
    check(!findLinkAt(parsed.screen, {1, 1}));
};

auto tOsc8AcrossSoftWrap = test("Links/osc8LinkSpansItsCellsAcrossTheWrap") = []
{
    const auto parsed = ParsedScreen {
        "0123456789abcdef\033]8;;https://x.test\033\\label text\033]8;;\033\\ after"};
    constexpr auto secondRow = 1L;
    constexpr auto labelStart = CellRef {0, 16};
    constexpr auto labelEnd = CellRef {secondRow, 5};

    const auto link = findLinkAt(parsed.screen, {secondRow, 1});

    check(link && link->uri == "https://x.test");
    check(link->span.start == labelStart && link->span.end == labelEnd);
    check(!findLinkAt(parsed.screen, {secondRow, 7}));
};

namespace
{
constexpr auto claudeCodeColumns = 60;
constexpr auto claudeCodeRows = 6;

struct ClaudeCodeScreen
{
    explicit ClaudeCodeScreen(const std::string& output)
    {
        parser.feed(output);
    }

    Theme theme;
    TermScreen screen {claudeCodeColumns, claudeCodeRows, theme};
    TermParser parser {screen, theme};
};

// Claude Code 2.1.326 in a 60-column terminal, recorded with
// scripts/record-fuzz-corpus.py: its prompt box echoing a typed URL, broken one
// column short of the edge, the box's grey background padding each row.
const auto claudeCodePromptEcho = std::string {
    "\033[48;2;55;55;55m\033[38;2;153;153;153m\xe2\x9d\xaf \033[38;2;255;255;255m"
    "Use the Bash tool to run exactly: echo \"PR: \033[39m              \r\033[1B"
    "  \033[38;2;255;255;255mhttps://github.com/jamierpond/CowTerm/pull/123456789/file"
    "\033[39m \r\033[1B"
    "  \033[38;2;255;255;255ms/abcdef0123456789abcdef#diff-deadbeef\" and then reply "
    "\033[39m   "};

// The same session's Bash tool line, broken right at the edge and indented by five.
const auto claudeCodeToolLine = std::string {
    "\033[38;2;153;153;153m  \xe2\x8e\xbf  $ echo \"PR: https://github.com/jamierpond/CowTerm/pull/"
    "\r\033[5C\033[1B123456789/files/abcdef0123456789abcdef#diff-deadbeef\"\033[39m"};

constexpr auto toolLineIndent = 5;
constexpr auto onTheToolLinesUrl = CellRef {0, 40};

const auto claudeCodeBrokenUrl = std::string {
    "https://github.com/jamierpond/CowTerm/pull/123456789/files/abcdef0123456789abcdef"
    "#diff-deadbeef"};

// Text that reaches one column short of the edge, ending in a URL, and a line break.
const auto rowEndingInUrlAtTheEdge =
    std::string {"see https://example.com/a/path/that/reaches/the/edge/of/xyz\r\n"};
} // namespace

auto tUrlInClaudeCodePrompt = test("Links/urlRunsOnIntoTheIndentedRowClaudeCodeBrokeItOnto") = []
{
    const auto parsed = ClaudeCodeScreen {claudeCodePromptEcho};
    constexpr auto firstRow = 1L;
    constexpr auto secondRow = 2L;
    constexpr auto indent = 2;
    constexpr auto lastUrlColumn = 39;
    constexpr auto wordsAfterTheUrl = lastUrlColumn + 3;
    constexpr auto onTheFirstPart = CellRef {firstRow, 30};
    constexpr auto onTheSecondPart = CellRef {secondRow, 10};

    const auto fromFirstRow = findLinkAt(parsed.screen, onTheFirstPart);
    const auto fromSecondRow = findLinkAt(parsed.screen, onTheSecondPart);

    check(fromFirstRow && fromFirstRow->uri == claudeCodeBrokenUrl);
    check(fromSecondRow && fromSecondRow->uri == claudeCodeBrokenUrl);
    check(fromFirstRow->span.start == CellRef {firstRow, indent});
    check(fromFirstRow->span.end == CellRef {secondRow, lastUrlColumn});
    check(!findLinkAt(parsed.screen, {secondRow, wordsAfterTheUrl}), "the words after it");
};

auto tUrlInClaudeCodeTool = test("Links/urlRunsOnIntoAClaudeCodeToolLineIndentedByFive") = []
{
    const auto parsed = ClaudeCodeScreen {claudeCodeToolLine};
    const auto link = findLinkAt(parsed.screen, onTheToolLinesUrl);

    check(link && link->uri == claudeCodeBrokenUrl);
    check(findLinkAt(parsed.screen, {1, toolLineIndent})->uri == claudeCodeBrokenUrl);
};

auto tIndentIsNotUnderlined = test("Links/theIndentBeforeAContinuedUrlIsNotPartOfTheLink") = []
{
    const auto parsed = ClaudeCodeScreen {claudeCodeToolLine};
    const auto span = findLinkAt(parsed.screen, onTheToolLinesUrl)->span;

    check(span.contains(onTheToolLinesUrl));
    check(span.contains({1, toolLineIndent}));
    check(!span.contains({1, 0}));
    check(!span.contains({1, toolLineIndent - 1}));
    check(!findLinkAt(parsed.screen, {1, 1}), "a click on the indent opens nothing");
};

auto tOrdinaryRowsAreNotJoined = test("Links/aUrlDoesNotTakeInTheNextRowUnlessItWasBroken") = []
{
    const auto urlEndingAtTheEdge = std::string {
        "https://example.com/a/path/that/reaches/the/edge/of/xyz"};
    constexpr auto onTheUrl = CellRef {0, 10};

    const auto continued = ClaudeCodeScreen {rowEndingInUrlAtTheEdge + "  more/path"};
    check(findLinkAt(continued.screen, onTheUrl)->uri == urlEndingAtTheEdge + "more/path");

    const auto notIndented = ClaudeCodeScreen {rowEndingInUrlAtTheEdge + "more"};
    check(findLinkAt(notIndented.screen, onTheUrl)->uri == urlEndingAtTheEdge);

    const auto listItem = ClaudeCodeScreen {rowEndingInUrlAtTheEdge + "  - item"};
    check(findLinkAt(listItem.screen, onTheUrl)->uri == urlEndingAtTheEdge);

    const auto numberedItem = ClaudeCodeScreen {rowEndingInUrlAtTheEdge + "  2. item"};
    check(findLinkAt(numberedItem.screen, onTheUrl)->uri == urlEndingAtTheEdge);

    const auto newUrl = ClaudeCodeScreen {rowEndingInUrlAtTheEdge + "  https://b.test/"};
    check(findLinkAt(newUrl.screen, onTheUrl)->uri == urlEndingAtTheEdge);

    const auto deepIndent = ClaudeCodeScreen {rowEndingInUrlAtTheEdge + "          far"};
    check(findLinkAt(deepIndent.screen, onTheUrl)->uri == urlEndingAtTheEdge);

    const auto shortRow = ClaudeCodeScreen {"see https://example.com/short\r\n  more"};
    check(findLinkAt(shortRow.screen, onTheUrl)->uri == "https://example.com/short");

    const auto sentenceEnd = ClaudeCodeScreen {
        "see https://example.com/a/path/that/reaches/the/edge/of/xy.\r\n  Then"};
    check(findLinkAt(sentenceEnd.screen, onTheUrl)->uri
          == "https://example.com/a/path/that/reaches/the/edge/of/xy");
};

auto tOsc8IgnoresIndentedRows = test("Links/osc8LinkStillSpansOnlyItsSoftWrappedCells") = []
{
    const auto parsed = ClaudeCodeScreen {
        "\033]8;;https://x.test/full\033\\0123456789012345678901234567890123456789"
        "01234567890123456789\033]8;;\033\\\r\n  \033]8;;https://x.test/full\033\\tail"
        "\033]8;;\033\\"};
    constexpr auto onTheLinkText = CellRef {0, 3};

    const auto link = findLinkAt(parsed.screen, onTheLinkText);

    check(link && link->uri == "https://x.test/full");
    check(link->span.end == CellRef {0, claudeCodeColumns - 1});
    check(link->span.indentedRowStarts.empty());
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
