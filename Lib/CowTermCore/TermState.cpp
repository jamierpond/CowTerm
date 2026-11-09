#include "TermParser.h"
#include "TermScreen.h"
#include "TermState.h"

#include <algorithm>
#include <array>
#include <deque>

namespace term
{
namespace
{
constexpr int currentFormatVersion = 2;
constexpr std::string_view hexDigits = "0123456789abcdef";
constexpr int bitsPerContinuationByte = 6;
constexpr char32_t continuationPayloadMask = 0x3f;
constexpr std::uint8_t continuationMarker = 0x80;

// UTF-8 without the validity rules: surrogates and anything up to 21 bits
// survive, because the parser can put such values in a cell.
void appendCodepoint(std::string& text, char32_t codepoint)
{
    auto appendByte = [&text](char32_t byte) { text += (char) (std::uint8_t) byte; };
    auto continuation = [](char32_t value, int shift)
    { return continuationMarker | ((value >> shift) & continuationPayloadMask); };

    if (codepoint < 0x80)
    {
        appendByte(codepoint);
    }
    else if (codepoint < 0x800)
    {
        appendByte(0xc0 | (codepoint >> 6));
        appendByte(continuation(codepoint, 0));
    }
    else if (codepoint < 0x10000)
    {
        appendByte(0xe0 | (codepoint >> 12));
        appendByte(continuation(codepoint, 6));
        appendByte(continuation(codepoint, 0));
    }
    else
    {
        appendByte(0xf0 | ((codepoint >> 18) & 0x07));
        appendByte(continuation(codepoint, 12));
        appendByte(continuation(codepoint, 6));
        appendByte(continuation(codepoint, 0));
    }
}

int getSequenceLength(std::uint8_t lead)
{
    if (lead < 0x80)
        return 1;

    if ((lead & 0xe0) == 0xc0)
        return 2;

    if ((lead & 0xf0) == 0xe0)
        return 3;

    return 4;
}

std::u32string decodeCodepoints(std::string_view text)
{
    constexpr std::array<std::uint8_t, 5> leadPayloadMask = {0, 0x7f, 0x1f, 0x0f, 0x07};
    auto codepoints = std::u32string {};
    auto index = std::size_t {0};

    while (index < text.size())
    {
        const auto lead = (std::uint8_t) text[index];
        const auto length = getSequenceLength(lead);
        auto codepoint = (char32_t) (lead & leadPayloadMask[(std::size_t) length]);

        for (auto offset = 1; offset < length && index + (std::size_t) offset < text.size();
             ++offset)
        {
            const auto byte = (std::uint8_t) text[index + (std::size_t) offset];
            codepoint = (codepoint << bitsPerContinuationByte)
                        | (byte & continuationPayloadMask);
        }

        codepoints += codepoint;
        index += (std::size_t) length;
    }

    return codepoints;
}

std::string encodeHex(std::string_view bytes)
{
    auto hex = std::string {};
    hex.reserve(bytes.size() * 2);

    for (const auto c: bytes)
    {
        const auto byte = (std::uint8_t) c;
        hex += hexDigits[byte >> 4];
        hex += hexDigits[byte & 0x0f];
    }

    return hex;
}

int getHexDigitValue(char digit)
{
    const auto position = hexDigits.find(digit);
    return position == std::string_view::npos ? 0 : (int) position;
}

std::string decodeHex(std::string_view hex)
{
    auto bytes = std::string {};

    for (auto index = std::size_t {0}; index + 1 < hex.size(); index += 2)
    {
        const auto high = getHexDigitValue(hex[index]);
        const auto low = getHexDigitValue(hex[index + 1]);
        bytes += (char) (std::uint8_t) ((high << 4) | low);
    }

    return bytes;
}

std::string encodeCodepoints(std::u32string_view codepoints)
{
    auto text = std::string {};

    for (const auto codepoint: codepoints)
        appendCodepoint(text, codepoint);

    return text;
}

std::int64_t getCellFlags(const Cell& cell)
{
    return (std::int64_t) (cell.content & ~Cell::valueMask);
}

bool isSameRun(const Cell& cell, const SavedCellRun& run)
{
    return cell.style == run.style && cell.hyperlink == run.hyperlink
           && getCellFlags(cell) == run.flags;
}

SavedLine saveLine(const Line& line)
{
    auto saved = SavedLine {};
    saved.wrapped = line.wrapped;

    for (const auto& cell: line)
    {
        if (saved.runs.empty() || !isSameRun(cell, saved.runs.back()))
            saved.runs.push_back({{}, cell.style, cell.hyperlink, getCellFlags(cell)});

        appendCodepoint(saved.runs.back().text, (char32_t) cell.value());
    }

    return saved;
}

template <typename Lines>
std::vector<SavedLine> saveLines(const Lines& lines)
{
    auto saved = std::vector<SavedLine> {};
    saved.reserve(lines.size());

    for (const auto& line: lines)
        saved.push_back(saveLine(line));

    return saved;
}

SavedKittyKeyboard saveKittyKeyboard(const KittyKeyboardStack& stack)
{
    const auto& entries = stack.getEntries();
    return {std::vector<int>(entries.begin(), entries.end()), (int) stack.getTop()};
}

void restoreKittyKeyboard(KittyKeyboardStack& stack, const SavedKittyKeyboard& saved)
{
    auto entries = KittyKeyboardStack::Entries {};
    const auto count = std::min(saved.entries.size(), entries.size());

    for (std::size_t index = 0; index < count; ++index)
        entries[index] = (std::uint8_t) saved.entries[index];

    const auto top = saved.top >= 0 ? (std::size_t) saved.top : KittyKeyboardStack::depth;
    stack.restore(entries, top);
}

// A saved table id's new id; ids the table never had become none.
std::uint32_t findNewId(const std::vector<std::uint32_t>& newIds, std::int64_t savedId)
{
    if (savedId <= 0 || savedId >= (std::int64_t) newIds.size())
        return 0;

    return newIds[(std::size_t) savedId];
}

SavedPen savePen(const Pen& pen)
{
    return {(std::int64_t) pen.fg,
            (std::int64_t) pen.bg,
            pen.attrs,
            pen.defaultFg,
            pen.defaultBg,
            pen.hyperlink};
}

SavedModes saveModes(const Modes& modes)
{
    return {modes.autowrap,
            modes.origin,
            modes.appCursorKeys,
            modes.appKeypad,
            modes.showCursor,
            modes.cursorBlink,
            modes.bracketedPaste,
            modes.altScreen,
            modes.mouseButtons,
            modes.mouseDrag,
            modes.mouseMotion,
            modes.mouseSgr,
            modes.synchronizedOutput,
            modes.focusEvents};
}

Modes restoreModes(const SavedModes& saved)
{
    return {saved.autowrap,
            saved.origin,
            saved.appCursorKeys,
            saved.appKeypad,
            saved.showCursor,
            saved.cursorBlink,
            saved.bracketedPaste,
            saved.altScreen,
            saved.mouseButtons,
            saved.mouseDrag,
            saved.mouseMotion,
            saved.mouseSgr,
            saved.synchronizedOutput,
            saved.focusEvents};
}
} // namespace

ScreenState TermScreen::saveState() const
{
    auto state = ScreenState {};
    state.columns = cols;
    state.rows = rowCount;
    state.primary = saveLines(grid);
    state.alternate = saveLines(altGrid);
    state.scrollback = saveLines(scrollback);
    state.cursor = {cursor.x, cursor.y};
    state.savedCursor = {savedCursor.x, savedCursor.y};
    state.pen = savePen(pen);
    state.savedPen = savePen(savedPen);
    state.modes = saveModes(modes);
    state.scrollTop = scrollTop;
    state.scrollBottom = scrollBottom;
    state.wrapPending = wrapPending;
    state.lastPrinted = (std::int64_t) lastPrinted;
    state.primaryKittyKeyboard = saveKittyKeyboard(primaryKittyKeyboard);
    state.alternateKittyKeyboard = saveKittyKeyboard(alternateKittyKeyboard);

    for (auto column = 0; column < (int) tabStops.size(); ++column)
    {
        if (tabStops[(std::size_t) column])
            state.tabStopColumns.push_back(column);
    }

    saveTables(state);
    return state;
}

void TermScreen::saveTables(ScreenState& state) const
{
    for (auto id = StyleTable::Id {1}; id <= styles.size(); ++id)
    {
        const auto& style = styles.find(id);
        state.styles.push_back({(std::int64_t) style.fg, (std::int64_t) style.bg, style.attrs});
    }

    for (auto id = HyperlinkTable::Id {1}; id <= hyperlinks.size(); ++id)
        state.hyperlinks.push_back(hyperlinks.find(id));

    for (auto id = GraphemeTable::Id {1}; id <= graphemes.size(); ++id)
        state.graphemes.push_back(encodeCodepoints(graphemes.find(id)));
}

// Default-coloured styles take the restoring theme's colours, so a state
// saved under one theme shows under another the way live output would.
CellStyle TermScreen::restoreStyle(const SavedStyle& saved) const
{
    auto style = CellStyle {(Rgb) saved.fg, (Rgb) saved.bg, (std::uint16_t) saved.attrs};

    if ((style.attrs & Attr::DefaultFg) != 0)
        style.fg = theme.foreground;

    if ((style.attrs & Attr::DefaultBg) != 0)
        style.bg = theme.background;

    return style;
}

TermScreen::RestoredTableIds TermScreen::restoreTables(const ScreenState& state)
{
    styles.clear();
    hyperlinks.clear();
    graphemes.clear();

    auto ids = RestoredTableIds {};
    ids.styles.push_back(0);
    ids.hyperlinks.push_back(0);
    ids.graphemes.push_back(0);

    for (const auto& saved: state.styles)
        ids.styles.push_back(internStyle(restoreStyle(saved)));

    for (const auto& uri: state.hyperlinks)
        ids.hyperlinks.push_back(hyperlinks.intern(uri));

    for (const auto& cluster: state.graphemes)
        ids.graphemes.push_back(graphemes.intern(decodeCodepoints(cluster)));

    return ids;
}

Cell TermScreen::restoreCell(char32_t value,
                             const SavedCellRun& run,
                             const RestoredTableIds& ids) const
{
    constexpr auto knownFlags =
        CellFlag::Wide | CellFlag::WideCont | CellFlag::WrapSpacer | CellFlag::Grapheme;

    auto cell = Cell {};
    cell.content = ((std::uint32_t) run.flags & knownFlags) | ((std::uint32_t) value & Cell::valueMask);
    cell.style = (StyleId) findNewId(ids.styles, run.style);
    cell.hyperlink = (HyperlinkId) findNewId(ids.hyperlinks, run.hyperlink);

    if (!cell.isGrapheme())
        return cell;

    const auto clusterId = findNewId(ids.graphemes, (std::int64_t) cell.value());

    if (clusterId == GraphemeTable::none)
        cell.setCodepoint(U' ');
    else
        cell.content = (cell.content & ~Cell::valueMask) | clusterId;

    return cell;
}

Line TermScreen::restoreLine(const SavedLine& saved, const RestoredTableIds& ids) const
{
    auto line = Line {};
    line.wrapped = saved.wrapped;

    for (const auto& run: saved.runs)
    {
        for (const auto value: decodeCodepoints(run.text))
            line.push_back(restoreCell(value, run, ids));
    }

    return line;
}

template <typename Lines>
Lines TermScreen::restoreLines(const std::vector<SavedLine>& saved, const RestoredTableIds& ids) const
{
    auto lines = Lines {};

    for (const auto& line: saved)
        lines.push_back(restoreLine(line, ids));

    return lines;
}

Pen TermScreen::restorePen(const SavedPen& saved, const RestoredTableIds& ids) const
{
    return {(Rgb) saved.fg,
            (Rgb) saved.bg,
            (std::uint16_t) saved.attrs,
            saved.defaultFg,
            saved.defaultBg,
            (HyperlinkId) findNewId(ids.hyperlinks, saved.hyperlink)};
}

void TermScreen::restoreState(const ScreenState& state)
{
    cols = std::max(state.columns, 1);
    rowCount = std::max(state.rows, 1);
    pen = Pen {};
    penStylesDirty = true;

    const auto ids = restoreTables(state);
    grid = restoreLines<std::vector<Line>>(state.primary, ids);
    altGrid = restoreLines<std::vector<Line>>(state.alternate, ids);
    scrollback = restoreLines<std::deque<Line>>(state.scrollback, ids);
    grid.resize((std::size_t) rowCount, blankLine());
    altGrid.resize((std::size_t) rowCount, blankLine());
    cursor = {state.cursor.x, state.cursor.y};
    savedCursor = {state.savedCursor.x, state.savedCursor.y};
    pen = restorePen(state.pen, ids);
    savedPen = restorePen(state.savedPen, ids);
    penStylesDirty = true;
    modes = restoreModes(state.modes);
    scrollTop = std::clamp(state.scrollTop, 0, rowCount - 1);
    scrollBottom = std::clamp(state.scrollBottom, scrollTop, rowCount - 1);
    wrapPending = state.wrapPending;
    lastPrinted = (char32_t) state.lastPrinted;
    restoreKittyKeyboard(primaryKittyKeyboard, state.primaryKittyKeyboard);
    restoreKittyKeyboard(alternateKittyKeyboard, state.alternateKittyKeyboard);
    tabStops.assign((std::size_t) cols, false);

    for (const auto column: state.tabStopColumns)
    {
        if (column >= 0 && column < cols)
            tabStops[(std::size_t) column] = true;
    }

    clampCursor();
    dirtyRows.assign((std::size_t) rowCount, true);
    markDirty();
}

ParserState TermParser::saveState() const
{
    auto saved = ParserState {};
    saved.state = (int) state;
    const auto partial = utf8.getPartialSequence();
    saved.utf8Codepoint = (std::int64_t) partial.codepoint;
    saved.utf8Remaining = partial.remainingContinuationBytes;
    const auto collected = params.getCollected();
    saved.params = collected.groups;
    saved.paramsNumberCount = (int) collected.numberCount;
    saved.paramsTooMany = collected.tooManyNumbers;
    saved.privateMarker = privateMarker;
    saved.intermediate = intermediate;
    saved.oscBufferHex = encodeHex(oscBuffer);
    saved.stringEscPending = stringEscPending;
    saved.charsetTarget = charsetTarget;
    saved.charsetZeroSpecial = charsetSpecial[0];
    saved.charsetOneSpecial = charsetSpecial[1];
    saved.activeCharset = activeCharset;
    saved.cursorStyle = cursorStyle;
    return saved;
}

void TermParser::restoreState(const ParserState& saved)
{
    state = State::Ground;

    for (const auto known: {State::Ground,
                            State::Escape,
                            State::Csi,
                            State::Osc,
                            State::StringSequence,
                            State::Charset})
    {
        if ((int) known == saved.state)
            state = known;
    }

    utf8.restorePartialSequence({(char32_t) saved.utf8Codepoint, saved.utf8Remaining});
    const auto numberCount = (std::size_t) std::max(saved.paramsNumberCount, 1);
    params.restoreCollected({saved.params, numberCount, saved.paramsTooMany});
    privateMarker = (char) saved.privateMarker;
    intermediate = (char) saved.intermediate;
    oscBuffer = decodeHex(saved.oscBufferHex);
    stringEscPending = saved.stringEscPending;
    charsetTarget = std::clamp(saved.charsetTarget, 0, 1);
    charsetSpecial[0] = saved.charsetZeroSpecial;
    charsetSpecial[1] = saved.charsetOneSpecial;
    activeCharset = std::clamp(saved.activeCharset, 0, 1);
    cursorStyle = saved.cursorStyle;
}

void startFreshShellBelow(TermScreen& screen, TermParser& parser)
{
    parser.restoreState({});

    if (screen.modes.altScreen)
        screen.kittyKeyboard().clear();

    screen.useAltScreen(false, true);
    screen.kittyKeyboard().clear();
    screen.modes = Modes {};
    auto resetRendition = CsiParameters {};
    resetRendition.clear();
    screen.applyGraphicRendition(resetRendition);
    screen.setHyperlink({});

    const auto cursor = screen.cursor;
    screen.setScrollRegion(0, screen.rows() - 1);
    screen.setCursor(cursor.x, cursor.y);

    if (screen.cursor.x > 0)
    {
        screen.carriageReturn();
        screen.linefeed();
    }
}

std::string serializeTerminalState(const TerminalState& state)
{
    return Miro::toJSONString(state);
}

std::optional<TerminalState> parseTerminalState(std::string_view json)
{
    try
    {
        constexpr int missingFormatVersion = -1;
        auto state = TerminalState {};
        state.formatVersion = missingFormatVersion;
        Miro::fromJSONString(state, json);

        if (state.formatVersion != currentFormatVersion)
            return std::nullopt;

        return state;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}
} // namespace term
