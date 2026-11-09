#include <CowTermCore/TerminalModel.h>

#include <NanoTest/NanoTest.h>

#include <string>
#include <string_view>

using namespace nano;
using namespace term;

namespace
{
constexpr int terminalColumns = 24;
constexpr int terminalRows = 6;
constexpr int historyLineCount = 40;

struct Terminal
{
    explicit Terminal(const Theme& themeToUse = {})
        : theme(themeToUse)
    {
    }

    TerminalState capture() const { return model.captureState(); }
    void restore(const TerminalState& state) { model.restoreState(state); }
    void feed(std::string_view bytes) { model.feed(bytes); }
    std::string serialize() const { return serializeTerminalState(capture()); }

    ResolvedCell resolveCellAt(int column, int row) const
    {
        const auto& line = screen.lineAt(row, 0);
        return screen.resolveCell(line[(std::size_t) column]);
    }

    Theme theme;
    TerminalModel model {terminalColumns, terminalRows, theme};
    TermScreen& screen = model.screen;
    TermParser& parser = model.parser;
};

// The terminal a fresh one becomes from original's serialized state.
Terminal restoreCopyOf(const Terminal& original)
{
    auto restored = Terminal {};
    restored.restore(*parseTerminalState(original.serialize()));
    return restored;
}

std::string makeBusySession()
{
    auto bytes = std::string {};

    for (auto line = 0; line < historyLineCount; ++line)
        bytes += "\033[3" + std::to_string(line % 8) + "mhistory " + std::to_string(line) + "\r\n";

    bytes += "\033[0m\033[1;4;38;2;10;20;30;48;5;200mstyled\033[0m ";
    bytes += "wide 漢字 and 🐮 \033(0lqk\033(B ";
    bytes += "\033]0;the title\007\033]7;file:///tmp\033\\";
    bytes += "\033[3g\033[1;5H\033H\033[2;5r\033[?1h\033[?2004h\033[?1006h\033[5 q";
    bytes += "\0337\033[3;3H\033[7mmoved\0338";
    bytes += "\033[?1049h\033[2J\033[Halternate screen text\033[4;1H\033[42m  ";
    bytes += "\033[38;5;1";
    return bytes;
}

std::string restoreThenSerialize(const std::string& json)
{
    const auto parsed = parseTerminalState(json);
    check(parsed.has_value());

    auto restored = Terminal {};
    restored.restore(*parsed);
    return restored.serialize();
}
} // namespace

auto tBusySessionRoundTrips = test("TerminalState/busySessionRoundTrips") = []
{
    auto original = Terminal {};
    original.parser.feed(makeBusySession());

    const auto json = original.serialize();

    check(restoreThenSerialize(json) == json);
};

auto tRestoredTerminalContinuesIdentically =
    test("TerminalState/restoredTerminalContinuesIdentically") = []
{
    const auto continuation = std::string {"6mred?\033[?1049l\r\nback on primary\r\n"};

    auto original = Terminal {};
    original.parser.feed(makeBusySession());

    auto restored = restoreCopyOf(original);

    original.parser.feed(continuation);
    restored.parser.feed(continuation);

    check(restored.serialize() == original.serialize());
};

// Snapshotting at every byte boundary covers each parser state a sequence
// can be cut off in: mid UTF-8, mid CSI parameters, mid OSC string.
auto tSnapshotAtEveryByteMatchesUninterrupted =
    test("TerminalState/snapshotAtEveryByteMatchesUninterrupted") = []
{
    const auto session =
        std::string {"a\xe6\xbc\xa2\033[1;38;2;1;2;3mb\033]2;ti\xc3\xa9tle\033\\c\033(0q\033(Bd\xed\xa0\x80\xf7\xbf\xbf\xbf\r\n"};

    auto uninterrupted = Terminal {};
    uninterrupted.parser.feed(session);
    const auto expected = uninterrupted.serialize();

    for (auto cut = std::size_t {0}; cut <= session.size(); ++cut)
    {
        auto before = Terminal {};
        before.parser.feed(std::string_view {session}.substr(0, cut));

        auto after = Terminal {};
        after.restore(*parseTerminalState(before.serialize()));
        after.parser.feed(std::string_view {session}.substr(cut));

        check(after.serialize() == expected);
    }
};

auto tDefaultColoursFollowTheRestoringTheme =
    test("TerminalState/defaultColoursFollowTheRestoringTheme") = []
{
    constexpr Rgb otherForeground = 0x112233;
    constexpr Rgb otherBackground = 0x445566;

    auto original = Terminal {};
    original.parser.feed("plain\033[31mred");

    auto otherTheme = Theme {};
    otherTheme.foreground = otherForeground;
    otherTheme.background = otherBackground;

    auto restored = Terminal {otherTheme};
    restored.restore(original.capture());

    check(restored.resolveCellAt(0, 0).fg == otherForeground);
    check(restored.resolveCellAt(0, 0).bg == otherBackground);
    check(restored.resolveCellAt(5, 0).fg == original.theme.ansi[1]);
};

auto tMalformedInputIsRejected = test("TerminalState/malformedInputIsRejected") = []
{
    check(!parseTerminalState("not json").has_value());
    check(!parseTerminalState(R"({"formatVersion": 99})").has_value());
};

auto tFreshShellStartsOnPrimaryScreenBelowOldOutput =
    test("TerminalState/freshShellStartsOnPrimaryScreenBelowOldOutput") = []
{
    auto terminal = Terminal {};
    terminal.parser.feed("$ vim\r\n\033[?1049h\033[?1000h\033[?2004h\033[?1004h\033[>5u"
                         "\033]8;;https://x.example\033\\\033[31mvim screen\033[2;4r\033[");

    startFreshShellBelow(terminal.screen, terminal.parser);
    terminal.parser.feed("prompt");

    check(!terminal.screen.modes.altScreen);
    check(!terminal.screen.modes.mouseButtons);
    check(!terminal.screen.modes.bracketedPaste);
    check(!terminal.screen.modes.focusEvents);
    check(terminal.screen.kittyKeyboard().current() == 0);
    check(terminal.resolveCellAt(0, 1).ch == U'p');
    check(terminal.resolveCellAt(0, 1).fg == terminal.theme.foreground);
    check(terminal.resolveCellAt(0, 1).hyperlink == noHyperlink);
};

auto tSynchronizedOutputModeRoundTrips = test("TerminalState/synchronizedOutputModeRoundTrips") = []
{
    auto original = Terminal {};
    original.feed("\033[?2026h");

    check(restoreCopyOf(original).screen.modes.synchronizedOutput);
};

auto tFocusReportingModeRoundTrips = test("TerminalState/focusReportingModeRoundTrips") = []
{
    auto original = Terminal {};
    original.feed("\033[?1004h");

    check(restoreCopyOf(original).screen.modes.focusEvents);
};

// Pushes on both screens, then pops back down: each screen's stack must
// come back whole, not just its current flags.
auto tKittyKeyboardStacksRoundTrip = test("TerminalState/kittyKeyboardStacksRoundTrip") = []
{
    constexpr std::uint8_t primaryFlags = 0b00101;
    constexpr std::uint8_t alternateFlags = 0b11111;

    auto original = Terminal {};
    original.feed("\033[>1u\033[>5u\033[?1049h\033[>3u\033[>31u");

    auto restored = restoreCopyOf(original);
    check(restored.screen.kittyKeyboard().current() == alternateFlags);

    restored.feed("\033[<u\033[?1049l");
    check(restored.screen.kittyKeyboard().current() == primaryFlags);

    restored.feed("\033[<u");
    check(restored.screen.kittyKeyboard().current() == KittyKeyboard::disambiguate);
};

auto tHyperlinksRoundTrip = test("TerminalState/hyperlinksRoundTrip") = []
{
    const auto uri = std::string {"https://example.com/cow"};

    auto original = Terminal {};
    original.feed("\033]8;;" + uri + "\033\\link\033]8;;\033\\ plain \033]8;;" + uri + "\033\\open");

    auto restored = restoreCopyOf(original);
    const auto linked = restored.resolveCellAt(0, 0).hyperlink;
    check(restored.screen.findHyperlinkUri(linked) == uri);
    check(restored.resolveCellAt(5, 0).hyperlink == noHyperlink);

    restored.feed("ed");
    check(restored.resolveCellAt(16, 0).hyperlink == linked);
};

auto tWrapFlagsRoundTripAndRewrap = test("TerminalState/wrapFlagsRoundTripAndRewrap") = []
{
    constexpr int narrowerColumns = 10;

    auto original = Terminal {};
    original.feed(std::string(terminalColumns + 6, 'w') + "\r\nshort");

    auto restored = restoreCopyOf(original);
    check(restored.screen.lineAt(0, 0).wrapped);
    check(!restored.screen.lineAt(1, 0).wrapped);

    original.screen.resize(narrowerColumns, terminalRows);
    restored.screen.resize(narrowerColumns, terminalRows);
    check(restored.serialize() == original.serialize());
};

auto tSharedStylesRoundTrip = test("TerminalState/sharedStylesRoundTrip") = []
{
    auto original = Terminal {};
    original.feed("\033[1;38;2;1;2;3mA\033[0;4;48;5;100mB\033[0;1;38;2;1;2;3mC");

    auto restored = restoreCopyOf(original);
    const auto first = restored.resolveCellAt(0, 0);
    const auto third = restored.resolveCellAt(2, 0);
    check(first.fg == 0x010203);
    check((first.attrs & Attr::Bold) != 0);
    check((restored.resolveCellAt(1, 0).attrs & Attr::Underline) != 0);
    check(restored.screen.lineAt(0, 0)[0].style == restored.screen.lineAt(0, 0)[2].style);
    check(third.fg == first.fg);
};

auto tGraphemeClustersRoundTrip = test("TerminalState/graphemeClustersRoundTrip") = []
{
    const auto family = std::u32string {0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467};
    const auto accented = std::u32string {U'e', 0x0301};

    auto original = Terminal {};
    original.feed("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7"
                  "e\xcc\x81");

    auto restored = restoreCopyOf(original);
    check(restored.resolveCellAt(0, 0).text == family);
    check((restored.resolveCellAt(0, 0).attrs & Attr::Wide) != 0);
    check(restored.resolveCellAt(2, 0).text == accented);

    original.feed("\xcc\x88");
    restored.feed("\xcc\x88");
    check(restored.serialize() == original.serialize());
};

auto tCutOffOverlongSequenceStaysDropped =
    test("TerminalState/cutOffOverlongSequenceStaysDropped") = []
{
    auto original = Terminal {};
    original.feed("\033[" + std::string(30, ';'));

    auto restored = restoreCopyOf(original);
    original.feed("1Hx");
    restored.feed("1Hx");
    check(restored.serialize() == original.serialize());
};

auto tOlderFormatIsRejected = test("TerminalState/olderFormatIsRejected") = []
{
    check(!parseTerminalState(R"({"formatVersion": 1})").has_value());
};
