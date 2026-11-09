#include <CowTermCore/TermParser.h>
#include <CowTermCore/TermScreen.h>

#include <NanoTest/NanoTest.h>

#include <string>
#include <string_view>

using namespace nano;
using namespace term;

namespace
{
struct ReflowScreen
{
    ReflowScreen(int columns, int rows)
        : screen {columns, rows, theme}
    {
    }

    std::string rowText(int visualRow, int scrollOffset = 0) const
    {
        auto text = std::string {};

        for (const auto& cell: screen.lineAt(visualRow, scrollOffset))
        {
            if ((cell.attrs & Attr::WideCont) == 0)
                text += cell.ch < 0x80 ? (char) cell.ch : '#';
        }

        while (!text.empty() && text.back() == ' ')
            text.pop_back();

        return text;
    }

    Theme theme;
    TermScreen screen;
    TermParser parser {screen, theme};
};
} // namespace

auto tNarrowingRewraps = test("Reflow/narrowingRewrapsALongLine") = []
{
    auto fed = ReflowScreen {10, 4};
    fed.parser.feed("abcdefghijklmno\r\nxy");

    fed.screen.resize(5, 4);

    check(fed.rowText(0) == "abcde");
    check(fed.rowText(1) == "fghij");
    check(fed.rowText(2) == "klmno");
    check(fed.rowText(3) == "xy");
    check(fed.screen.cursor.y == 3);
    check(fed.screen.cursor.x == 2);
};

auto tWideningRejoins = test("Reflow/wideningRejoinsWrappedRows") = []
{
    auto fed = ReflowScreen {5, 4};
    fed.parser.feed("abcdefghijklmno\r\nxy");

    fed.screen.resize(20, 4);

    check(fed.rowText(0) == "abcdefghijklmno");
    check(fed.rowText(1) == "xy");
    check(fed.screen.cursor.y == 1);
    check(fed.screen.cursor.x == 2);
};

auto tHardNewlinesStay = test("Reflow/hardNewlinesAreNotJoined") = []
{
    auto fed = ReflowScreen {10, 3};
    fed.parser.feed("abc\r\ndef");

    fed.screen.resize(20, 3);

    check(fed.rowText(0) == "abc");
    check(fed.rowText(1) == "def");
};

auto tScrollbackRewraps = test("Reflow/scrollbackRewrapsToo") = []
{
    constexpr auto lineCount = 20;
    auto fed = ReflowScreen {10, 3};

    for (auto line = 0; line < lineCount; ++line)
        fed.parser.feed("0123456789abcde\r\n");

    const auto historyBefore = fed.screen.scrollbackSize();
    fed.screen.resize(20, 3);

    check(fed.screen.scrollbackSize() < historyBefore);
    check(fed.rowText(0, fed.screen.scrollbackSize()) == "0123456789abcde");
};

auto tWideCharacterMovesWhole = test("Reflow/aWideCharacterNeverSplits") = []
{
    auto fed = ReflowScreen {10, 3};
    fed.parser.feed("abcd\xe6\xbc\xa2" "ef");

    fed.screen.resize(5, 3);

    check(fed.rowText(0) == "abcd");
    check(fed.rowText(1) == "#ef");
};

auto tEraseEndsTheWrap = test("Reflow/erasingToTheEndStopsARowRunningOn") = []
{
    auto fed = ReflowScreen {5, 4};
    fed.parser.feed("abcdefgh\033[1;3H\033[K");

    fed.screen.resize(10, 4);

    check(fed.rowText(0) == "ab");
    check(fed.rowText(1) == "fgh");
};

auto tCursorKeepsItsPlaceInTheText = test("Reflow/theCursorKeepsItsPlaceInTheText") = []
{
    auto fed = ReflowScreen {10, 3};
    fed.parser.feed("hello world\033[1;3H");

    fed.screen.resize(4, 3);

    check(fed.screen.cursor.x == 2);
    check(fed.rowText(fed.screen.cursor.y).substr(0, 4) == "hell");
};

auto tAlternateScreenIsNotReflowed = test("Reflow/theAlternateScreenIsNotReflowed") = []
{
    auto fed = ReflowScreen {10, 3};
    fed.parser.feed("\033[?1049habcdefghijkl");

    fed.screen.resize(20, 3);

    check(fed.rowText(0) == "abcdefghij");
    check(fed.rowText(1) == "kl");
};
