#pragma once

#include <Miro/Reflect.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace term
{
// Neighbouring cells with one style, link and set of flags. text holds each
// cell's value as one UTF-8 codepoint: the codepoint itself, or for a cell
// flagged as a grapheme cluster, the cluster's index in ScreenState::graphemes.
struct SavedCellRun
{
    std::string text;
    int style = 0;
    int hyperlink = 0;
    std::int64_t flags = 0;

    MIRO_REFLECT(text, style, hyperlink, flags)
};

struct SavedLine
{
    std::vector<SavedCellRun> runs;
    bool wrapped = false;

    MIRO_REFLECT(runs, wrapped)
};

// One entry of the shared style table; a cell's style 0 is the theme default.
struct SavedStyle
{
    std::int64_t fg = 0;
    std::int64_t bg = 0;
    int attrs = 0;

    MIRO_REFLECT(fg, bg, attrs)
};

// A screen's kitty keyboard flags stack, oldest slot first, and its top.
struct SavedKittyKeyboard
{
    std::vector<int> entries;
    int top = 0;

    MIRO_REFLECT(entries, top)
};

struct SavedCursor
{
    int x = 0;
    int y = 0;

    MIRO_REFLECT(x, y)
};

struct SavedPen
{
    std::int64_t fg = 0;
    std::int64_t bg = 0;
    int attrs = 0;
    bool defaultFg = true;
    bool defaultBg = true;
    int hyperlink = 0;

    MIRO_REFLECT(fg, bg, attrs, defaultFg, defaultBg, hyperlink)
};

struct SavedModes
{
    bool autowrap = true;
    bool origin = false;
    bool appCursorKeys = false;
    bool appKeypad = false;
    bool showCursor = true;
    bool cursorBlink = true;
    bool bracketedPaste = false;
    bool altScreen = false;
    bool mouseButtons = false;
    bool mouseDrag = false;
    bool mouseMotion = false;
    bool mouseSgr = false;
    bool synchronizedOutput = false;
    bool focusEvents = false;

    MIRO_REFLECT(autowrap,
                 origin,
                 appCursorKeys,
                 appKeypad,
                 showCursor,
                 cursorBlink,
                 bracketedPaste,
                 altScreen,
                 mouseButtons,
                 mouseDrag,
                 mouseMotion,
                 mouseSgr,
                 synchronizedOutput,
                 focusEvents)
};

// Everything TermScreen holds: both grids, history oldest first, the style,
// hyperlink and grapheme tables their cells point into (ids are 1-based
// positions in these lists), cursor and pen with their DECSC copies, modes,
// margins, tab stops and each screen's kitty keyboard flags.
struct ScreenState
{
    int columns = 0;
    int rows = 0;
    std::vector<SavedLine> primary;
    std::vector<SavedLine> alternate;
    std::vector<SavedLine> scrollback;
    SavedCursor cursor;
    SavedCursor savedCursor;
    SavedPen pen;
    SavedPen savedPen;
    SavedModes modes;
    int scrollTop = 0;
    int scrollBottom = 0;
    bool wrapPending = false;
    std::int64_t lastPrinted = 0;
    std::vector<int> tabStopColumns;
    std::vector<SavedStyle> styles;
    std::vector<std::string> hyperlinks;
    std::vector<std::string> graphemes;
    SavedKittyKeyboard primaryKittyKeyboard;
    SavedKittyKeyboard alternateKittyKeyboard;

    MIRO_REFLECT(columns,
                 rows,
                 primary,
                 alternate,
                 scrollback,
                 cursor,
                 savedCursor,
                 pen,
                 savedPen,
                 modes,
                 scrollTop,
                 scrollBottom,
                 wrapPending,
                 lastPrinted,
                 tabStopColumns,
                 styles,
                 hyperlinks,
                 graphemes,
                 primaryKittyKeyboard,
                 alternateKittyKeyboard)
};

// TermParser's machine, including a sequence cut off mid-way, so bytes
// that follow a restore finish it exactly as they would have before.
// Byte strings (the OSC buffer) are hex, since they need not be UTF-8.
struct ParserState
{
    int state = 0;
    std::int64_t utf8Codepoint = 0;
    int utf8Remaining = 0;
    std::vector<std::vector<int>> params;
    int paramsNumberCount = 1;
    bool paramsTooMany = false;
    int privateMarker = 0;
    int intermediate = 0;
    std::string oscBufferHex;
    bool stringEscPending = false;
    int charsetTarget = 0;
    bool charsetZeroSpecial = false;
    bool charsetOneSpecial = false;
    int activeCharset = 0;
    int cursorStyle = 0;

    MIRO_REFLECT(state,
                 utf8Codepoint,
                 utf8Remaining,
                 params,
                 paramsNumberCount,
                 paramsTooMany,
                 privateMarker,
                 intermediate,
                 oscBufferHex,
                 stringEscPending,
                 charsetTarget,
                 charsetZeroSpecial,
                 charsetOneSpecial,
                 activeCharset,
                 cursorStyle)
};

// A whole terminal: what a pane needs to come back exactly as it was.
struct TerminalState
{
    int formatVersion = 2;
    ScreenState screen;
    ParserState parser;
    std::string title;
    std::string workingDirectory;

    MIRO_REFLECT(formatVersion, screen, parser, title, workingDirectory)
};

std::string serializeTerminalState(const TerminalState& state);

// Empty for malformed input or a format this build does not read.
std::optional<TerminalState> parseTerminalState(std::string_view json);
} // namespace term
