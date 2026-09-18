#include <CowTermCore/TermParser.h>
#include <CowTermCore/TermScreen.h>

#include <NanoTest/NanoTest.h>

#include <string_view>

using namespace nano;
using namespace term;

// The cell colours a full-screen TUI paints a selected row with. lazygit, fzf,
// ranger and the rest all highlight the same way -- a background colour over
// the row, in one of the four SGR spellings below -- so a background that does
// not survive the parser is a row that does not look selected.
namespace
{
struct Parsed
{
    explicit Parsed(std::string_view bytes)
    {
        auto parser = TermParser {screen, theme};
        parser.feed(bytes);
    }

    const Cell& cell(int column = 0) const
    {
        return screen.lineAt(0, 0)[(std::size_t) column];
    }

    Theme theme;
    TermScreen screen {20, 4, theme};
};
} // namespace

auto tAnsiBackground = test("ScreenColor/ansiBackgroundIsRecorded") = []
{
    const auto parsed = Parsed {"\033[41mX"};

    check(parsed.cell().bg == parsed.theme.ansi[1]);
};

auto tIndexedBackground = test("ScreenColor/indexedBackgroundIsRecorded") = []
{
    const auto parsed = Parsed {"\033[48;5;33mX"};

    check(parsed.cell().bg != parsed.theme.background);
};

auto tTrueColorBackground = test("ScreenColor/trueColorBackgroundIsRecorded") = []
{
    const auto parsed = Parsed {"\033[48;2;255;0;255mX"};

    check(parsed.cell().bg == Rgb {0xFF00FF});
};

auto tBrightBackground = test("ScreenColor/brightBackgroundIsRecorded") = []
{
    const auto parsed = Parsed {"\033[100mX"};

    check(parsed.cell().bg == parsed.theme.ansi[8]);
};

// The other way a row gets highlighted: same colours, swapped at draw time.
auto tReverseVideo = test("ScreenColor/reverseVideoSetsTheAttribute") = []
{
    const auto parsed = Parsed {"\033[7mX"};

    check((parsed.cell().attrs & Attr::Inverse) != 0);
};

// A highlight has to end where the row does, or it bleeds down the screen.
auto tResetClearsBackground = test("ScreenColor/resetReturnsToTheDefault") = []
{
    const auto parsed = Parsed {"\033[41mX\033[0mY"};

    check(parsed.cell(0).bg == parsed.theme.ansi[1]);
    check(parsed.cell(1).bg == parsed.theme.background);
};

// SGR 49 is the targeted form, which a TUI redrawing one row uses in place of a
// full reset so it keeps the foreground it just set.
auto tDefaultBackgroundCode = test("ScreenColor/sgr49ReturnsToTheDefault") = []
{
    const auto parsed = Parsed {"\033[41mX\033[49mY"};

    check(parsed.cell(0).bg == parsed.theme.ansi[1]);
    check(parsed.cell(1).bg == parsed.theme.background);
};

// How a TUI actually writes a highlighted row: position, colour, text, reset.
// The whole run has to carry the background, not just the first cell.
auto tHighlightedRowRun = test("ScreenColor/aHighlightedRowKeepsItsBackground") = []
{
    const auto parsed = Parsed {"\033[2;1H\033[48;5;238m  selected  \033[0m"};

    const auto& row = parsed.screen.lineAt(1, 0);

    for (auto column = 0; column < 12; ++column)
        check(row[(std::size_t) column].bg != parsed.theme.background,
              "every cell of the run carries the highlight");
};
