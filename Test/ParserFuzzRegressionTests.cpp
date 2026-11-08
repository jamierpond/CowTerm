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
