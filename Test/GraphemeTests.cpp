#include <CowTermCore/TermParser.h>
#include <CowTermCore/TermScreen.h>
#include <CowTermTerminal/CellText.h>

#include <NanoTest/NanoTest.h>

#include <string>
#include <string_view>

using namespace nano;
using namespace term;

namespace
{
struct GraphemeScreen
{
    explicit GraphemeScreen(std::string_view bytes)
    {
        parser.feed(bytes);
    }

    ResolvedCell cell(int column) const
    {
        return screen.resolveCell(screen.lineAt(0, 0)[(std::size_t) column]);
    }

    std::string rowText() const { return extractVisibleRowText(screen, screen.lineAt(0, 0), screen.columns()); }

    Theme theme;
    TermScreen screen {20, 3, theme};
    TermParser parser {screen, theme};
};

bool isWide(const ResolvedCell& cell)
{
    return (cell.attrs & Attr::Wide) != 0;
}
} // namespace

auto tCombiningMarkJoins = test("Graphemes/aCombiningMarkJoinsItsLetter") = []
{
    const auto fed = GraphemeScreen {"e\xcc\x81x"};

    check(fed.cell(0).text == std::u32string {char32_t {0x65}, char32_t {0x301}});
    check(fed.cell(1).ch == U'x');
    check(fed.screen.cursor.x == 2);
    check(fed.rowText() == "e\xcc\x81x");
};

auto tZwjFamilyIsOneCell = test("Graphemes/aZwjFamilyIsOneWideCell") = []
{
    const auto fed = GraphemeScreen {"\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7!"};

    check(fed.cell(0).text == std::u32string {char32_t {0x1f468}, char32_t {0x200d}, char32_t {0x1f469}, char32_t {0x200d}, char32_t {0x1f467}});
    check(isWide(fed.cell(0)));
    check(fed.cell(2).ch == U'!');
    check(fed.screen.cursor.x == 3);
};

auto tSkinToneJoins = test("Graphemes/aSkinToneModifierJoins") = []
{
    const auto fed = GraphemeScreen {"\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd."};

    check(fed.cell(0).text == std::u32string {char32_t {0x1f44d}, char32_t {0x1f3fd}});
    check(fed.cell(2).ch == U'.');
};

auto tFlagIsOneCluster = test("Graphemes/twoRegionalIndicatorsMakeOneFlag") = []
{
    const auto fed = GraphemeScreen {"\xf0\x9f\x87\xba\xf0\x9f\x87\xb8\xf0\x9f\x87\xac\xf0\x9f\x87\xa7"};

    check(fed.cell(0).text == std::u32string {char32_t {0x1f1fa}, char32_t {0x1f1f8}});
    check(fed.cell(1).text == U"\U0001F1EC\U0001F1E7");
};

auto tEmojiPresentationWidens = test("Graphemes/vs16WidensANarrowSymbol") = []
{
    const auto fed = GraphemeScreen {"\xe2\x9d\xa4\xef\xb8\x8fx"};

    check(isWide(fed.cell(0)));
    check(fed.cell(2).ch == U'x');
};

auto tMarkAfterSpaceIsDropped = test("Graphemes/aMarkWithNothingBeforeItIsDropped") = []
{
    const auto fed = GraphemeScreen {"\xcc\x81" "a"};

    check(fed.cell(0).ch == U'a');
    check(fed.cell(0).text.empty());
};

auto tStylesAreShared = test("Styles/aFullStyleTableDropsUnusedStyles") = []
{
    constexpr auto styleCount = 70000;
    auto fed = GraphemeScreen {""};

    for (auto index = 0; index < styleCount; ++index)
    {
        const auto red = std::to_string(index % 256);
        const auto green = std::to_string(index / 256 % 256);
        const auto blue = std::to_string(index / 65536);
        fed.parser.feed("\033[38;2;" + red + ";" + green + ";" + blue + "mX\r");
    }

    const auto last = styleCount - 1;
    const auto expected = (Rgb) (((last % 256) << 16) | ((last / 256 % 256) << 8) | (last / 65536));
    check(fed.cell(0).fg == expected);
};
