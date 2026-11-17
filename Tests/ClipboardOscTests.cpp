#include <Emulator/TermParser.h>
#include <Emulator/TermScreen.h>

#include <NanoTest/NanoTest.h>
#include <eacp/Core/Utils/Base64.h>

#include <string>
#include <string_view>
#include <vector>

using namespace nano;
using namespace term;

namespace
{
constexpr auto bellTerminator = std::string_view {"\a"};
constexpr auto stringTerminator = std::string_view {"\033\\"};

struct ClipboardParser
{
    ClipboardParser()
    {
        parser.respond = [this](std::string_view bytes) { responses += bytes; };
        parser.onCopyToClipboard = [this](const std::string& text)
        { copies.push_back(text); };
    }

    Theme theme;
    TermScreen screen {20, 4, theme};
    TermParser parser {screen, theme};
    std::string responses;
    std::vector<std::string> copies;
};

std::string makeClipboardOsc(std::string_view targets,
                             std::string_view encoded,
                             std::string_view terminator)
{
    return "\033]52;" + std::string {targets} + ";" + std::string {encoded}
           + std::string {terminator};
}

std::string makeYankOfLength(std::size_t length)
{
    constexpr auto yankedLine = std::string_view {"yanked line of code\n"};
    auto text = std::string {};

    while (text.size() < length)
        text += yankedLine;

    text.resize(length);
    return text;
}
} // namespace

auto tClipboardPlainText = test("Clipboard/osc52CopiesPlainText") = []
{
    auto fed = ClipboardParser {};

    fed.parser.feed(makeClipboardOsc("c", "eWFua2VkIGxpbmU=", bellTerminator));

    check(fed.copies == std::vector<std::string> {"yanked line"});
};

auto tClipboardUtf8 = test("Clipboard/osc52CopiesUtf8Text") = []
{
    auto fed = ClipboardParser {};

    fed.parser.feed(
        makeClipboardOsc("c", "aMOpbGxvIOKGkiDkuJbnlYw=", bellTerminator));

    check(fed.copies == std::vector<std::string> {"héllo → 世界"});
};

auto tClipboardAnyTargets = test("Clipboard/osc52CopiesForEmptyOrAnyTargets") = []
{
    auto fed = ClipboardParser {};

    fed.parser.feed(makeClipboardOsc("", "YQ==", bellTerminator));
    fed.parser.feed(makeClipboardOsc("p", "Yg==", bellTerminator));
    fed.parser.feed(makeClipboardOsc("cps", "Yw==", bellTerminator));

    check(fed.copies == std::vector<std::string> {"a", "b", "c"});
};

auto tClipboardStringTerminator =
    test("Clipboard/osc52EndedByEscBackslashCopies") = []
{
    auto fed = ClipboardParser {};

    fed.parser.feed(makeClipboardOsc("c", "eWFua2VkIGxpbmU=", stringTerminator)
                    + "x");

    check(fed.copies == std::vector<std::string> {"yanked line"});
    check(fed.screen.lineAt(0, 0)[0].codepoint() == U'x');
};

auto tClipboardReadRequestIgnored =
    test("Clipboard/osc52ReadRequestIsNeverAnswered") = []
{
    auto fed = ClipboardParser {};

    fed.parser.feed(makeClipboardOsc("c", "?", bellTerminator));
    fed.parser.feed(makeClipboardOsc("c", "?", stringTerminator));

    check(fed.copies.empty());
    check(fed.responses.empty());
};

auto tClipboardInvalidBase64 =
    test("Clipboard/osc52WithInvalidBase64OrNoTextCopiesNothing") = []
{
    auto fed = ClipboardParser {};

    fed.parser.feed(makeClipboardOsc("c", "not base64!", bellTerminator));
    fed.parser.feed(makeClipboardOsc("c", "", bellTerminator));
    fed.parser.feed("\033]52;eWFua2VkIGxpbmU=\a");

    check(fed.copies.empty());
};

auto tClipboardLongYank =
    test("Clipboard/osc52CopiesAYankFarPastTheOtherCodesLimit") = []
{
    constexpr auto yankLength = std::size_t {200 * 1024};
    constexpr auto chunkLength = std::size_t {4096};
    auto fed = ClipboardParser {};
    const auto yank = makeYankOfLength(yankLength);
    const auto sequence =
        makeClipboardOsc("c", eacp::Base64::encode(yank), bellTerminator);

    for (auto offset = std::size_t {0}; offset < sequence.size();
         offset += chunkLength)
        fed.parser.feed(std::string_view {sequence}.substr(offset, chunkLength));

    check(fed.copies.size() == 1);
    check(fed.copies.front() == yank);
};

auto tClipboardOverLimitCopiesNothing =
    test("Clipboard/osc52OverTheLimitCopiesNothing") = []
{
    constexpr auto yankFarLargerThanTheLimit = std::size_t {16 * 1024 * 1024};
    auto fed = ClipboardParser {};
    const auto yank = makeYankOfLength(yankFarLargerThanTheLimit);

    fed.parser.feed(makeClipboardOsc("c", eacp::Base64::encode(yank), bellTerminator)
                    + "x");
    fed.parser.feed(makeClipboardOsc("c", "eWFua2VkIGxpbmU=", stringTerminator));

    check(fed.copies == std::vector<std::string> {"yanked line"});
    check(fed.screen.lineAt(0, 0)[0].codepoint() == U'x');
};

auto tLongTitleStillTruncated =
    test("Clipboard/overLongTitleIsStillCutAndApplied") = []
{
    constexpr auto titleLength = std::size_t {10000};
    constexpr auto keptTitleLength = std::size_t {4096};
    auto fed = ClipboardParser {};
    auto title = std::string {};
    fed.parser.onTitleChanged = [&title](const std::string& newTitle)
    { title = newTitle; };

    fed.parser.feed("\033]2;" + std::string(titleLength, 't') + "\a");

    check(title.size() == keptTitleLength - std::string_view {"2;"}.size());
};
