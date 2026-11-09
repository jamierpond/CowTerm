#include <CowTermCore/TermParser.h>
#include <CowTermCore/TermScreen.h>

#include <NanoTest/NanoTest.h>

#include <string_view>
#include <vector>

using namespace nano;
using namespace term;

namespace
{
constexpr auto screenColumns = 10;
constexpr auto screenRows = 5;

struct DirtyRowScreen
{
    DirtyRowScreen()
    {
        screen.clearDirtyRows();
    }

    void feed(std::string_view bytes) { parser.feed(bytes); }

    std::vector<int> findDirtyRows() const
    {
        auto rows = std::vector<int> {};

        for (auto row = 0; row < screen.rows(); ++row)
        {
            if (screen.isRowDirty(row))
                rows.push_back(row);
        }

        return rows;
    }

    Theme theme;
    TermScreen screen {screenColumns, screenRows, theme};
    TermParser parser {screen, theme};
};

using Rows = std::vector<int>;
} // namespace

auto tNewScreenIsAllDirty = test("DirtyRows/aNewScreenIsAllDirty") = []
{
    auto theme = Theme {};
    auto screen = TermScreen {screenColumns, screenRows, theme};

    for (auto row = 0; row < screenRows; ++row)
        check(screen.isRowDirty(row));
};

auto tPrintMarksItsRow = test("DirtyRows/printingMarksOnlyItsRow") = []
{
    auto s = DirtyRowScreen {};
    s.feed("\033[3;1Hhi");
    check(s.findDirtyRows() == Rows {2});
};

auto tCursorMoveMarksNothing = test("DirtyRows/movingTheCursorMarksNoRow") = []
{
    auto s = DirtyRowScreen {};
    s.feed("\033[4;6H\r\n\b");
    check(s.findDirtyRows().empty());
};

auto tScrollMarksTheRegion = test("DirtyRows/scrollingMarksTheScrollRegion") = []
{
    auto s = DirtyRowScreen {};
    s.feed("\033[2;4r");
    s.screen.clearDirtyRows();
    s.feed("\033[1S");
    check(s.findDirtyRows() == Rows {1, 2, 3});
};

auto tLinefeedAtBottomMarksAll = test("DirtyRows/aLinefeedAtTheBottomMarksEveryRow") = []
{
    auto s = DirtyRowScreen {};
    s.feed("\033[5;1H\n");
    check(s.findDirtyRows() == Rows {0, 1, 2, 3, 4});
};

auto tEraseLineMarksItsRow = test("DirtyRows/eraseInLineMarksOnlyItsRow") = []
{
    auto s = DirtyRowScreen {};
    s.feed("\033[2;3H\033[K");
    check(s.findDirtyRows() == Rows {1});
};

auto tEraseBelowMarksFromCursor = test("DirtyRows/eraseBelowMarksTheCursorRowAndBelow") = []
{
    auto s = DirtyRowScreen {};
    s.feed("\033[3;1H\033[J");
    check(s.findDirtyRows() == Rows {2, 3, 4});
};

auto tClearMarksAll = test("DirtyRows/clearingTheScreenMarksEveryRow") = []
{
    auto s = DirtyRowScreen {};
    s.feed("\033[2J");
    check(s.findDirtyRows() == Rows {0, 1, 2, 3, 4});
};

auto tCharacterEditsMarkTheRow = test("DirtyRows/insertDeleteAndEraseCharactersMarkTheRow") = []
{
    auto s = DirtyRowScreen {};
    s.feed("\033[4;2H\033[2@");
    check(s.findDirtyRows() == Rows {3});

    s.screen.clearDirtyRows();
    s.feed("\033[2P");
    check(s.findDirtyRows() == Rows {3});

    s.screen.clearDirtyRows();
    s.feed("\033[2X");
    check(s.findDirtyRows() == Rows {3});
};

auto tAltScreenMarksAll = test("DirtyRows/switchingToTheAlternateScreenMarksEveryRow") = []
{
    auto s = DirtyRowScreen {};
    s.feed("\033[?1049h");
    check(s.findDirtyRows() == Rows {0, 1, 2, 3, 4});
};

auto tResizeMarksAll = test("DirtyRows/resizingMarksEveryRow") = []
{
    auto s = DirtyRowScreen {};
    s.screen.resize(screenColumns, screenRows + 1);
    check(s.findDirtyRows() == Rows {0, 1, 2, 3, 4, 5});
};

auto tClearDirtyRows = test("DirtyRows/clearingLeavesNoDirtyRow") = []
{
    auto s = DirtyRowScreen {};
    s.feed("abc");
    s.screen.clearDirtyRows();
    check(s.findDirtyRows().empty());
};
