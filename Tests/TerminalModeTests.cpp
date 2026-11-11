#include <CowTermCore/FocusReports.h>
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

auto tHyperlinkMarksCells = test("Hyperlinks/osc8MarksTheCellsBetweenStartAndEnd") = []
{
    auto fed = ModeParser {};

    fed.parser.feed("a\033]8;id=1;https://x.test\033\\bc\033]8;;\033\\d");

    const auto& line = fed.screen.lineAt(0, 0);
    check(line[0].hyperlink == noHyperlink);
    check(fed.screen.findHyperlinkUri(line[1].hyperlink) == "https://x.test");
    check(line[2].hyperlink == line[1].hyperlink);
    check(line[3].hyperlink == noHyperlink);
};

auto tHyperlinkTableDropsUnusedLinks = test("Hyperlinks/aFullTableDropsLinksNoCellUses") = []
{
    constexpr auto linkCount = 5000;
    auto fed = ModeParser {};

    for (auto index = 0; index < linkCount; ++index)
        fed.parser.feed("\033]8;;https://x.test/" + std::to_string(index) + "\033\\\r\033]8;;\033\\");

    fed.parser.feed("\033]8;;https://last.test\033\\L\033]8;;\033\\");

    const auto& line = fed.screen.lineAt(0, 0);
    check(fed.screen.findHyperlinkUri(line[0].hyperlink) == "https://last.test");
};

auto tHyperlinkKeepsUsedLinksThroughCompaction = test("Hyperlinks/linksStillOnScreenSurviveCompaction") = []
{
    constexpr auto linkCount = 5000;
    auto fed = ModeParser {};
    fed.parser.feed("\033]8;;https://kept.test\033\\K\033]8;;\033\\\r\n");

    for (auto index = 0; index < linkCount; ++index)
        fed.parser.feed("\033]8;;https://x.test/" + std::to_string(index) + "\033\\\r\033]8;;\033\\");

    const auto& line = fed.screen.lineAt(0, 0);
    check(fed.screen.findHyperlinkUri(line[0].hyperlink) == "https://kept.test");
};

auto tOpenableSchemes = test("Hyperlinks/onlyWebMailAndFileSchemesAreOpenable") = []
{
    check(hasOpenableScheme("https://a"));
    check(hasOpenableScheme("http://a"));
    check(hasOpenableScheme("mailto:a@b"));
    check(hasOpenableScheme("file:///tmp"));
    check(!hasOpenableScheme("x-app://run"));
    check(!hasOpenableScheme(""));
};

auto tFocusEventsMode = test("Modes/mode1004TurnsFocusEventsOn") = []
{
    auto fed = ModeParser {};

    fed.parser.feed("\033[?1004h");

    check(fed.screen.modes.focusEvents);
};

auto tFocusReportsOnlyChanges = test("Modes/focusReportsSendOnlyChanges") = []
{
    auto reports = FocusReports {};

    check(reports.update(true, true) == "\033[I");
    check(reports.update(true, true).empty());
    check(reports.update(false, true) == "\033[O");
};

auto tFocusReportsNeedTheMode = test("Modes/focusReportsNeedMode1004") = []
{
    auto reports = FocusReports {};

    check(reports.update(true, false).empty());
    check(reports.update(false, true) == "\033[O");
};

auto tKittyPushPopQuery = test("KittyKeyboard/pushQueryAndPop") = []
{
    auto fed = ModeParser {};

    fed.parser.feed("\033[>1u\033[?u");
    check(fed.responses == "\033[?1u");
    fed.responses.clear();

    fed.parser.feed("\033[>31u\033[?u\033[<u\033[?u\033[<u\033[?u");
    check(fed.responses == "\033[?31u\033[?1u\033[?0u");
};

auto tKittySetModes = test("KittyKeyboard/setAddAndRemoveFlags") = []
{
    auto fed = ModeParser {};

    fed.parser.feed("\033[=1u\033[=4;2u\033[?u");
    check(fed.responses == "\033[?5u");
    fed.responses.clear();

    fed.parser.feed("\033[=1;3u\033[?u");
    check(fed.responses == "\033[?4u");
};

auto tKittyFlagsPerScreen = test("KittyKeyboard/eachScreenKeepsItsOwnFlags") = []
{
    auto fed = ModeParser {};

    fed.parser.feed("\033[>1u\033[?1049h");
    check(fed.screen.kittyKeyboard().current() == 0);

    fed.parser.feed("\033[>3u\033[?1049l");
    check(fed.screen.kittyKeyboard().current() == 1);
};

auto tKittyPopPastTheBottom = test("KittyKeyboard/poppingEverythingClearsTheStack") = []
{
    auto fed = ModeParser {};

    fed.parser.feed("\033[>1u\033[>2u\033[<100u");

    check(fed.screen.kittyKeyboard().current() == 0);
};

auto tCsiUStillRestoresTheCursor = test("KittyKeyboard/plainCsiUStillRestoresTheCursor") = []
{
    auto fed = ModeParser {};

    fed.parser.feed("\033[2;3H\033[s\033[H\033[u");

    check(fed.screen.cursor.x == 2);
    check(fed.screen.cursor.y == 1);
};
