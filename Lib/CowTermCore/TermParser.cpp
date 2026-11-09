#include "TermParser.h"

#include "SelectGraphicRendition.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace term
{
namespace
{
constexpr std::uint8_t bellByte = 0x07;
constexpr std::uint8_t escapeByte = 0x1b;
constexpr std::uint8_t cancelByte = 0x18;
constexpr std::uint8_t substituteByte = 0x1a;
constexpr std::uint8_t deleteByte = 0x7f;
constexpr std::uint8_t firstPrintableByte = 0x20;
constexpr std::uint8_t firstPrivateMarker = 0x3c;
constexpr std::uint8_t lastPrivateMarker = 0x3f;
constexpr std::uint8_t firstIntermediate = 0x20;
constexpr std::uint8_t lastIntermediate = 0x2f;
constexpr std::uint8_t firstFinalByte = 0x40;
constexpr std::uint8_t lastFinalByte = 0x7e;
constexpr auto largestOscLength = std::size_t {4096};

// DEC special graphics (ESC ( 0): the 0x60..0x7e range becomes line-drawing
// glyphs. Everything full-screen apps draw borders with on a non-UTF-8 path.
constexpr char32_t firstDecGraphicsCodepoint = 0x60;
constexpr char32_t lastDecGraphicsCodepoint = 0x7e;
constexpr std::array<char32_t, 31> decGraphics = {
    U'◆', U'▒', U'␉', U'␌', U'␍', U'␊', U'°', U'±', U'␤', U'␋', U'┘',
    U'┐', U'┌', U'└', U'┼', U'⎺', U'⎻', U'─', U'⎼', U'⎽', U'├', U'┤',
    U'┴', U'┬', U'│', U'≤', U'≥', U'π', U'≠', U'£', U'·'};

bool isInRange(std::uint8_t byte, std::uint8_t first, std::uint8_t last)
{
    return byte >= first && byte <= last;
}

bool isControlByte(std::uint8_t byte)
{
    return byte < firstPrintableByte || byte == deleteByte;
}

bool isDecGraphicsCodepoint(char32_t codepoint)
{
    return codepoint >= firstDecGraphicsCodepoint && codepoint <= lastDecGraphicsCodepoint;
}

template <typename Entry, std::size_t entryCount, typename Key>
std::optional<Entry> findEntry(const std::array<Entry, entryCount>& table, Key key)
{
    const auto found = std::find_if(
        table.begin(), table.end(), [key](const Entry& entry) { return entry.key == key; });

    if (found == table.end())
        return std::nullopt;

    return *found;
}

// file://host/percent%20encoded/path -> /percent encoded/path
std::string pathFromFileUrl(const std::string& url)
{
    constexpr auto fileScheme = std::string_view {"file://"};
    constexpr auto hexBase = 16;
    constexpr auto escapedByteLength = std::size_t {2};

    if (!url.starts_with(fileScheme))
        return {};

    const auto pathStart = url.find('/', fileScheme.size());

    if (pathStart == std::string::npos)
        return {};

    const auto path = url.substr(pathStart);
    auto decoded = std::string {};
    decoded.reserve(path.size());

    for (std::size_t i = 0; i < path.size(); ++i)
    {
        if (path[i] == '%' && i + escapedByteLength < path.size())
        {
            const auto hex = path.substr(i + 1, escapedByteLength);
            decoded.push_back((char) std::strtol(hex.c_str(), nullptr, hexBase));
            i += escapedByteLength;
        }
        else
        {
            decoded.push_back(path[i]);
        }
    }

    return decoded;
}

// 777;notify;title;body -> "title: body"
std::optional<std::string> notificationFromOsc777(const std::string& payload)
{
    const auto kindEnd = payload.find(';');

    if (kindEnd == std::string::npos || payload.substr(0, kindEnd) != "notify")
        return std::nullopt;

    auto text = payload.substr(kindEnd + 1);
    const auto titleEnd = text.find(';');

    if (titleEnd != std::string::npos)
        text = text.substr(0, titleEnd) + ": " + text.substr(titleEnd + 1);

    return text;
}

std::string formatColorQueryReply(std::string_view code, Rgb rgb)
{
    constexpr auto replyCapacity = std::size_t {64};
    constexpr auto byteMask = 0xffu;
    const auto red = (rgb >> 16) & byteMask;
    const auto green = (rgb >> 8) & byteMask;
    const auto blue = rgb & byteMask;
    const auto codeText = std::string {code};

    char reply[replyCapacity];
    std::snprintf(reply,
                  sizeof(reply),
                  "\033]%s;rgb:%02x%02x/%02x%02x/%02x%02x\033\\",
                  codeText.c_str(),
                  red,
                  red,
                  green,
                  green,
                  blue,
                  blue);
    return reply;
}
} // namespace

struct TermParserActions
{
    using ByteAction = void (*)(TermParser&);

    struct ByteEntry
    {
        std::uint8_t key;
        ByteAction run;
    };

    struct OscEntry
    {
        std::string_view key;
        void (*run)(TermParser&, const std::string& payload);
    };

    struct ModeEntry
    {
        int key;
        void (*run)(TermScreen&, bool on);
    };

    struct ModeFlagEntry
    {
        int key;
        bool Modes::*flag;
    };

    static void moveCursorUp(TermParser& p) { p.screen.moveCursor(0, -p.params.count(0)); }
    static void moveCursorDown(TermParser& p) { p.screen.moveCursor(0, p.params.count(0)); }
    static void moveCursorRight(TermParser& p) { p.screen.moveCursor(p.params.count(0), 0); }
    static void moveCursorLeft(TermParser& p) { p.screen.moveCursor(-p.params.count(0), 0); }

    static void moveToNextLine(TermParser& p)
    {
        moveCursorDown(p);
        p.screen.carriageReturn();
    }

    static void moveToPreviousLine(TermParser& p)
    {
        moveCursorUp(p);
        p.screen.carriageReturn();
    }

    static void setColumn(TermParser& p) { p.screen.setColumn(p.params.count(0) - 1); }
    static void setRow(TermParser& p) { p.screen.setRow(p.params.count(0) - 1); }

    static void setCursor(TermParser& p)
    {
        p.screen.setCursor(p.params.count(1) - 1, p.params.count(0) - 1);
    }

    static void setScrollRegion(TermParser& p)
    {
        p.screen.setScrollRegion(p.params.value(0, 1) - 1,
                                 p.params.value(1, p.screen.rows()) - 1);
    }

    static void answerDeviceAttributes(TermParser& p)
    {
        if (p.privateMarker == '>')
            p.respond("\033[>0;95;0c");
        else if (p.privateMarker == 0)
            p.respond("\033[?6c");
    }

    static void setCursorStyle(TermParser& p)
    {
        if (p.intermediate == ' ')
            p.cursorStyle = p.params.value(0, 0);
    }

    static void fullReset(TermParser& p)
    {
        p.screen.fullReset();
        p.charsetSpecial = {false, false};
        p.activeCharset = 0;
        p.cursorStyle = 0;
    }

    static void selectCharset(TermParser& p, int target)
    {
        p.charsetTarget = target;
        p.state = TermParser::State::Charset;
    }

    static void setOrigin(TermScreen& screen, bool on)
    {
        screen.modes.origin = on;
        screen.setCursor(0, 0);
    }

    static void saveOrRestoreCursor(TermScreen& screen, bool on)
    {
        if (on)
            screen.saveCursor();
        else
            screen.restoreCursor();
    }

    static constexpr std::array<ByteEntry, 33> csiActions = {{
        {'@', [](TermParser& p) { p.screen.insertChars(p.params.count(0)); }},
        {'A', moveCursorUp},
        {'B', moveCursorDown},
        {'e', moveCursorDown},
        {'C', moveCursorRight},
        {'a', moveCursorRight},
        {'D', moveCursorLeft},
        {'E', moveToNextLine},
        {'F', moveToPreviousLine},
        {'G', setColumn},
        {'`', setColumn},
        {'H', setCursor},
        {'f', setCursor},
        {'I', [](TermParser& p) { p.screen.horizontalTab(p.params.count(0)); }},
        {'J', [](TermParser& p) { p.screen.eraseInDisplay(p.params.value(0, 0)); }},
        {'K', [](TermParser& p) { p.screen.eraseInLine(p.params.value(0, 0)); }},
        {'L', [](TermParser& p) { p.screen.insertLines(p.params.count(0)); }},
        {'M', [](TermParser& p) { p.screen.deleteLines(p.params.count(0)); }},
        {'P', [](TermParser& p) { p.screen.deleteChars(p.params.count(0)); }},
        {'S', [](TermParser& p) { p.screen.scrollUp(p.params.count(0)); }},
        {'T', [](TermParser& p) { p.screen.scrollDown(p.params.count(0)); }},
        {'X', [](TermParser& p) { p.screen.eraseChars(p.params.count(0)); }},
        {'Z', [](TermParser& p) { p.screen.backTab(p.params.count(0)); }},
        {'b', [](TermParser& p) { p.screen.repeatLastChar(p.params.count(0)); }},
        {'c', answerDeviceAttributes},
        {'d', setRow},
        {'g', [](TermParser& p) { p.screen.clearTabStops(p.params.value(0, 0)); }},
        {'m', [](TermParser& p) { applySelectGraphicRendition(p.screen.pen, p.params, p.theme); }},
        {'n', [](TermParser& p) { p.reportDeviceStatus(); }},
        {'q', setCursorStyle},
        {'r', setScrollRegion},
        {'s', [](TermParser& p) { p.screen.saveCursor(); }},
        {'u', [](TermParser& p) { p.screen.restoreCursor(); }},
    }};

    static constexpr std::array<ByteEntry, 18> escapeActions = {{
        {'[', [](TermParser& p) { p.startSequence(TermParser::State::Csi); }},
        {']', [](TermParser& p) { p.startSequence(TermParser::State::Osc); }},
        {'P', [](TermParser& p) { p.startSequence(TermParser::State::StringSequence); }},
        {'X', [](TermParser& p) { p.startSequence(TermParser::State::StringSequence); }},
        {'^', [](TermParser& p) { p.startSequence(TermParser::State::StringSequence); }},
        {'_', [](TermParser& p) { p.startSequence(TermParser::State::StringSequence); }},
        {'(', [](TermParser& p) { selectCharset(p, 0); }},
        {')', [](TermParser& p) { selectCharset(p, 1); }},
        {'7', [](TermParser& p) { p.screen.saveCursor(); }},
        {'8', [](TermParser& p) { p.screen.restoreCursor(); }},
        {'D', [](TermParser& p) { p.screen.linefeed(); }},
        {'E',
         [](TermParser& p)
         {
             p.screen.carriageReturn();
             p.screen.linefeed();
         }},
        {'H', [](TermParser& p) { p.screen.setTabStop(); }},
        {'M', [](TermParser& p) { p.screen.reverseIndex(); }},
        {'Z', [](TermParser& p) { p.respond("\033[?6c"); }},
        {'c', fullReset},
        {'=', [](TermParser& p) { p.screen.modes.appKeypad = true; }},
        {'>', [](TermParser& p) { p.screen.modes.appKeypad = false; }},
    }};

    static constexpr std::array<ByteEntry, 9> controlActions = {{
        {0x07, [](TermParser& p) { p.onBell(); }},
        {0x08, [](TermParser& p) { p.screen.backspace(); }},
        {0x09, [](TermParser& p) { p.screen.horizontalTab(); }},
        {0x0a, [](TermParser& p) { p.screen.linefeed(); }},
        {0x0b, [](TermParser& p) { p.screen.linefeed(); }},
        {0x0c, [](TermParser& p) { p.screen.linefeed(); }},
        {0x0d, [](TermParser& p) { p.screen.carriageReturn(); }},
        {0x0e, [](TermParser& p) { p.activeCharset = 1; }},
        {0x0f, [](TermParser& p) { p.activeCharset = 0; }},
    }};

    static constexpr std::array<ModeFlagEntry, 9> privateModeFlags = {{
        {1, &Modes::appCursorKeys},
        {7, &Modes::autowrap},
        {12, &Modes::cursorBlink},
        {25, &Modes::showCursor},
        {1000, &Modes::mouseButtons},
        {1002, &Modes::mouseDrag},
        {1003, &Modes::mouseMotion},
        {1006, &Modes::mouseSgr},
        {2004, &Modes::bracketedPaste},
    }};

    static constexpr std::array<ModeEntry, 5> privateModeActions = {{
        {6, setOrigin},
        {47, [](TermScreen& screen, bool on) { screen.useAltScreen(on, false); }},
        {1047, [](TermScreen& screen, bool on) { screen.useAltScreen(on, false); }},
        {1048, saveOrRestoreCursor},
        {1049, [](TermScreen& screen, bool on) { screen.useAltScreen(on, true); }},
    }};

    static void reportTitle(TermParser& p, const std::string& payload) { p.onTitleChanged(payload); }

    static void reportCwd(TermParser& p, const std::string& payload)
    {
        if (const auto path = pathFromFileUrl(payload); !path.empty())
            p.onCwdChanged(path);
    }

    static void reportNotification(TermParser& p, const std::string& payload)
    {
        if (const auto text = notificationFromOsc777(payload))
            p.onNotify(*text);
    }

    // Color queries: vim probes these to pick its default background.
    static void answerColorQuery(TermParser& p, std::string_view code, Rgb rgb, const std::string& payload)
    {
        if (payload == "?")
            p.respond(formatColorQueryReply(code, rgb));
    }

    static constexpr std::array<OscEntry, 7> oscActions = {{
        {"0", reportTitle},
        {"2", reportTitle},
        {"7", reportCwd},
        {"9", [](TermParser& p, const std::string& payload) { p.onNotify(payload); }},
        {"777", reportNotification},
        {"10",
         [](TermParser& p, const std::string& payload)
         { answerColorQuery(p, "10", p.theme.foreground, payload); }},
        {"11",
         [](TermParser& p, const std::string& payload)
         { answerColorQuery(p, "11", p.theme.background, payload); }},
    }};
};

TermParser::TermParser(TermScreen& screenToUse, const Theme& themeToUse)
    : screen(screenToUse)
    , theme(themeToUse)
{
}

void TermParser::feed(std::string_view bytes)
{
    for (const auto c: bytes)
        processByte((std::uint8_t) c);
}

void TermParser::startSequence(State next)
{
    state = next;
    params.clear();
    privateMarker = 0;
    intermediate = 0;
    oscBuffer.clear();
    stringEscPending = false;
}

void TermParser::processByte(std::uint8_t byte)
{
    switch (state)
    {
        case State::Ground:
            processGroundByte(byte);
            return;
        case State::Escape:
            dispatchEscape(byte);
            return;
        case State::Csi:
            processCsiByte(byte);
            return;
        case State::Osc:
            processOscByte(byte);
            return;
        case State::StringSequence:
            processStringSequenceByte(byte);
            return;
        case State::Charset:
            processCharsetByte(byte);
            return;
    }
}

void TermParser::processGroundByte(std::uint8_t byte)
{
    if (byte == escapeByte)
    {
        utf8.reset();
        state = State::Escape;
        return;
    }

    if (isControlByte(byte))
    {
        executeControl(byte);
        return;
    }

    utf8.decode(byte, [this](char32_t codepoint) { putCodepoint(codepoint); });
}

void TermParser::processCsiByte(std::uint8_t byte)
{
    if (byte == escapeByte)
    {
        state = State::Escape;
        return;
    }

    if (byte == cancelByte || byte == substituteByte)
    {
        state = State::Ground;
        return;
    }

    if (byte < firstPrintableByte)
    {
        executeControl(byte);
        return;
    }

    collectCsiByte(byte);
}

void TermParser::collectCsiByte(std::uint8_t byte)
{
    if (isInRange(byte, '0', '9'))
        params.appendDigit(byte - '0');
    else if (byte == ';')
        params.startGroup();
    else if (byte == ':')
        params.startSubParameter();
    else if (isInRange(byte, firstPrivateMarker, lastPrivateMarker))
        privateMarker = (char) byte;
    else if (isInRange(byte, firstIntermediate, lastIntermediate))
        intermediate = (char) byte;
    else if (isInRange(byte, firstFinalByte, lastFinalByte))
    {
        dispatchCsi(byte);
        state = State::Ground;
    }
}

void TermParser::processOscByte(std::uint8_t byte)
{
    if (byte == bellByte)
    {
        dispatchOsc();
        state = State::Ground;
        return;
    }

    if (stringEscPending)
    {
        stringEscPending = false;
        dispatchOsc();
        dispatchEscape(byte);
        return;
    }

    if (byte == escapeByte)
        stringEscPending = true;
    else if (oscBuffer.size() < largestOscLength)
        oscBuffer.push_back((char) byte);
}

// DCS/SOS/PM/APC content is consumed and dropped.
void TermParser::processStringSequenceByte(std::uint8_t byte)
{
    if (byte == bellByte)
    {
        state = State::Ground;
        return;
    }

    if (stringEscPending)
    {
        stringEscPending = false;
        dispatchEscape(byte);
        return;
    }

    if (byte == escapeByte)
        stringEscPending = true;
}

void TermParser::processCharsetByte(std::uint8_t byte)
{
    charsetSpecial[(std::size_t) charsetTarget] = byte == '0';
    state = State::Ground;
}

void TermParser::executeControl(std::uint8_t byte)
{
    if (const auto action = findEntry(TermParserActions::controlActions, byte))
        action->run(*this);
}

void TermParser::dispatchEscape(std::uint8_t byte)
{
    state = State::Ground;

    if (const auto action = findEntry(TermParserActions::escapeActions, byte))
        action->run(*this);
}

void TermParser::dispatchCsi(std::uint8_t finalByte)
{
    if (!params.hasTooManyNumbers())
        runCsi(finalByte);
}

void TermParser::runCsi(std::uint8_t finalByte)
{
    const auto isModeChange = finalByte == 'h' || finalByte == 'l';

    if (privateMarker == '?' && isModeChange)
    {
        for (std::size_t i = 0; i < params.size(); ++i)
            setPrivateMode(params.value(i, -1), finalByte == 'h');

        return;
    }

    if (privateMarker != 0 && finalByte != 'c')
        return;

    if (const auto action = findEntry(TermParserActions::csiActions, finalByte))
        action->run(*this);
}

void TermParser::reportDeviceStatus()
{
    constexpr auto statusRequest = 5;
    constexpr auto cursorPositionRequest = 6;
    const auto request = params.value(0, 0);

    if (request == statusRequest)
        respond("\033[0n");
    else if (request == cursorPositionRequest)
        respond("\033[" + std::to_string(screen.cursor.y + 1) + ";"
                + std::to_string(screen.cursor.x + 1) + "R");
}

void TermParser::setPrivateMode(int mode, bool on)
{
    if (const auto flag = findEntry(TermParserActions::privateModeFlags, mode))
    {
        screen.modes.*(flag->flag) = on;
        return;
    }

    if (const auto action = findEntry(TermParserActions::privateModeActions, mode))
        action->run(screen, on);
}

void TermParser::dispatchOsc()
{
    const auto separator = oscBuffer.find(';');

    if (separator == std::string::npos)
        return;

    const auto code = std::string_view {oscBuffer}.substr(0, separator);

    if (const auto action = findEntry(TermParserActions::oscActions, code))
        action->run(*this, oscBuffer.substr(separator + 1));
}

void TermParser::putCodepoint(char32_t codepoint)
{
    if (charsetSpecial[(std::size_t) activeCharset] && isDecGraphicsCodepoint(codepoint))
        codepoint = decGraphics[codepoint - firstDecGraphicsCodepoint];

    screen.putChar(codepoint);
}
} // namespace term
