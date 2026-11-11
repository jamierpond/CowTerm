#include <CowTermCore/TermParser.h>
#include <CowTermCore/TermScreen.h>

#include <NanoTest/NanoTest.h>

#include <string>
#include <string_view>

using namespace nano;
using namespace term;

// Inputs the parser fuzzer (Fuzz/ParserFuzzer.cpp) once crashed on.
namespace
{
struct FedParser
{
    explicit FedParser(std::string_view bytes, int columns = 20, int rows = 4)
        : screen {columns, rows, theme}
    {
        parser.onCwdChanged = [this](const std::string& path) { reportedCwd = path; };
        parser.feed(bytes);
    }

    Theme theme;
    TermScreen screen;
    TermParser parser {screen, theme};
    std::string reportedCwd;
};
} // namespace

auto tCwdUrlWithoutPath = test("ParserFuzz/cwdUrlWithOnlyAHostIsIgnored") = []
{
    const auto fed = FedParser {"\033]7;file://host\007"};

    check(fed.reportedCwd.empty());
};

auto tCwdUrlWithPath = test("ParserFuzz/cwdUrlWithAPathIsDecoded") = []
{
    const auto fed = FedParser {"\033]7;file://host/a%20b\007"};

    check(fed.reportedCwd == "/a b");
};

namespace
{
constexpr auto cursorRightBy = std::string_view {"5C"};

std::string makeCursorRightWithLeadingNumbers(int leadingNumberCount)
{
    auto sequence = std::string {"\033["};

    for (auto index = 0; index < leadingNumberCount; ++index)
        sequence += "1;";

    return sequence + std::string {cursorRightBy};
}
} // namespace

auto tCsiAtTheNumberLimitRuns = test("ParserFuzz/csiWithTwentyFourNumbersRuns") = []
{
    constexpr auto leadingNumbers = 23;
    const auto fed = FedParser {makeCursorRightWithLeadingNumbers(leadingNumbers)};

    check(fed.screen.cursor.x == 1);
};

auto tCsiPastTheNumberLimitIsDropped = test("ParserFuzz/csiWithTwentyFiveNumbersIsDropped") = []
{
    constexpr auto leadingNumbers = 24;
    const auto fed = FedParser {makeCursorRightWithLeadingNumbers(leadingNumbers)};

    check(fed.screen.cursor.x == 0);
};

auto tThousandsOfSeparatorsAreDropped = test("ParserFuzz/thousandsOfSeparatorsAreDropped") = []
{
    constexpr auto separatorCount = 100000;
    const auto fed = FedParser {"\033[" + std::string(separatorCount, ';') + "5CA"};

    check(fed.screen.cursor.x == 1);
};
