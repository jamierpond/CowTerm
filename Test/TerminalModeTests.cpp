#include <CowTermCore/TermParser.h>
#include <CowTermCore/TermScreen.h>

#include <NanoTest/NanoTest.h>

#include <string>
#include <string_view>

using namespace nano;
using namespace term;

namespace
{
struct ModeParser
{
    ModeParser()
    {
        parser.respond = [this](std::string_view bytes) { responses += bytes; };
        parser.onSynchronizedOutputStarted = [this] { ++synchronizedOutputStarts; };
    }

    Theme theme;
    TermScreen screen {20, 4, theme};
    TermParser parser {screen, theme};
    std::string responses;
    int synchronizedOutputStarts = 0;
};
} // namespace

auto tSynchronizedOutputSets = test("Modes/synchronizedOutputSetsAndResets") = []
{
    auto fed = ModeParser {};

    fed.parser.feed("\033[?2026h");
    check(fed.screen.modes.synchronizedOutput);
    check(fed.synchronizedOutputStarts == 1);

    fed.parser.feed("\033[?2026l");
    check(!fed.screen.modes.synchronizedOutput);
    check(fed.synchronizedOutputStarts == 1);
};

auto tFullResetEndsSynchronizedOutput = test("Modes/fullResetEndsSynchronizedOutput") = []
{
    auto fed = ModeParser {};

    fed.parser.feed("\033[?2026h\033c");

    check(!fed.screen.modes.synchronizedOutput);
};

auto tModeQueryReportsSet = test("Modes/modeQueryReportsSynchronizedOutputSet") = []
{
    auto fed = ModeParser {};

    fed.parser.feed("\033[?2026h\033[?2026$p");

    check(fed.responses == "\033[?2026;1$y");
};

auto tModeQueryReportsReset = test("Modes/modeQueryReportsSynchronizedOutputReset") = []
{
    auto fed = ModeParser {};

    fed.parser.feed("\033[?2026$p");

    check(fed.responses == "\033[?2026;2$y");
};

auto tModeQueryReportsUnknown = test("Modes/modeQueryReportsAnUnknownModeAsZero") = []
{
    auto fed = ModeParser {};

    fed.parser.feed("\033[?31337$p");

    check(fed.responses == "\033[?31337;0$y");
};
