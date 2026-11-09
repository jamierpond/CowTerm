#pragma once

#include "CsiParameters.h"
#include "HyperlinkTable.h"
#include "KittyKeyboardFlags.h"
#include "TermState.h"
#include "TermTypes.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace term
{
// One row of cells. wrapped marks a row the text ran off the end of, so it
// continues on the next row; resizing rejoins and rewraps such rows.
struct Line : std::vector<Cell>
{
    using std::vector<Cell>::vector;

    bool wrapped = false;
};

struct CursorPos
{
    int x = 0;
    int y = 0;
};

struct Pen
{
    Rgb fg = 0;
    Rgb bg = 0;
    std::uint16_t attrs = 0;
    bool defaultFg = true;
    bool defaultBg = true;
    HyperlinkId hyperlink = noHyperlink;
};

struct Modes
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
};

// The terminal's cell grid: primary + alternate buffers, scrollback, cursor,
// scroll region and the erase/insert/delete operations the parser dispatches.
// All coordinates are 0-based; erases and scrolls fill with the pen's
// background (BCE), which full-screen apps rely on.
class TermScreen
{
public:
    TermScreen(int colsToUse, int rowsToUse, const Theme& themeToUse);

    int columns() const { return cols; }
    int rows() const { return rowCount; }

    void putChar(char32_t cp);
    void repeatLastChar(int count);

    void linefeed();
    void carriageReturn();
    void backspace();
    void horizontalTab(int count = 1);
    void backTab(int count);
    void reverseIndex();
    void setTabStop();
    void clearTabStops(int mode);

    void setCursor(int col, int row);
    void setColumn(int col);
    void setRow(int row);
    void moveCursor(int dx, int dy);
    void saveCursor();
    void restoreCursor();

    void eraseInDisplay(int mode);
    void eraseInLine(int mode);
    void insertLines(int count);
    void deleteLines(int count);
    void insertChars(int count);
    void deleteChars(int count);
    void eraseChars(int count);
    void scrollUp(int count);
    void scrollDown(int count);
    void setScrollRegion(int top, int bottom);

    void useAltScreen(bool on, bool saveCursorAndClear);
    void fullReset();
    void resize(int newCols, int newRows);

    int scrollbackSize() const { return (int) scrollback.size(); }
    const Line& lineAt(int visualRow, int scrollOffset) const;

    // OSC 8: cells printed from here on link to uri; an empty uri ends the link.
    void setHyperlink(std::string_view uri);
    std::string_view findHyperlinkUri(HyperlinkId id) const;

    // SGR (CSI ... m) on the pen.
    void applyGraphicRendition(const CsiParameters& parameters);

    // A cell with its style, cluster and flags looked up. The cluster view
    // lasts until the screen next changes.
    ResolvedCell resolveCell(const Cell& cell) const;
    std::vector<ResolvedCell> resolveLine(const Line& line) const;

    // The active screen's kitty keyboard flags; each screen keeps its own.
    KittyKeyboardStack& kittyKeyboard();
    const KittyKeyboardStack& kittyKeyboard() const;

    ScreenState saveState() const;
    void restoreState(const ScreenState& state);

    // Bumped on every visible change; the view repaints when it moves.
    std::uint64_t version() const { return changeVersion; }

    // Rows of the active screen whose cells changed since clearDirtyRows; the
    // renderer rebuilds only these. Cursor moves do not mark a row.
    bool isRowDirty(int visualRow) const;
    void clearDirtyRows();

    Modes modes;
    CursorPos cursor;

private:
    using StyleTable = InternTable<CellStyle, 65535, CellStyleHash>;
    using GraphemeTable = InternTable<std::u32string, 65535>;

    struct ClusterTarget
    {
        int x = 0;
        int y = 0;
    };

    // Each saved table id's id in the rebuilt tables, indexed by saved id.
    struct RestoredTableIds
    {
        std::vector<std::uint32_t> styles;
        std::vector<std::uint32_t> hyperlinks;
        std::vector<std::uint32_t> graphemes;
    };

    void saveTables(ScreenState& state) const;
    CellStyle restoreStyle(const SavedStyle& saved) const;
    RestoredTableIds restoreTables(const ScreenState& state);
    Cell restoreCell(char32_t value, const SavedCellRun& run, const RestoredTableIds& ids) const;
    Line restoreLine(const SavedLine& saved, const RestoredTableIds& ids) const;
    template <typename Lines>
    Lines restoreLines(const std::vector<SavedLine>& saved, const RestoredTableIds& ids) const;
    Pen restorePen(const SavedPen& saved, const RestoredTableIds& ids) const;

    void compactTables();
    StyleId internStyle(const CellStyle& style);
    void refreshPenStyles();
    const CellStyle& findStyle(StyleId id) const;
    std::u32string findClusterText(const Cell& cell) const;
    std::optional<ClusterTarget> findClusterToExtend(char32_t codepoint) const;
    void extendCluster(ClusterTarget target, char32_t codepoint);
    void widenForEmojiPresentation(ClusterTarget target);
    Line& row(int index) { return activeGrid()[(std::size_t) index]; }
    std::vector<Line>& activeGrid() { return modes.altScreen ? altGrid : grid; }
    const std::vector<Line>& activeGrid() const
    {
        return modes.altScreen ? altGrid : grid;
    }

    std::uint16_t defaultColorAttributes() const;
    Cell blankCell();
    Cell penCell(char32_t codepoint);
    Line blankLine();
    bool isOnScreen(int x, int y) const;
    bool isScrollingIntoHistory(int top) const;
    bool isAlreadySize(int newCols, int newRows) const;
    void markDirty() { ++changeVersion; }
    void markRowDirty(int y);
    void markRowsDirty(int fromY, int toYExclusive);
    void markAllRowsDirty();
    void clampCursor();
    void writeCell(int x, int y, const Cell& cell);

    // Overwriting either half of a wide pair blanks the partner cell so no
    // orphaned halves survive.
    void damageWidePair(int x, int y);
    void wrapIfPending();
    void makeRoomForWideCharacter();
    void writeWideCell(Cell cell);
    void advanceAfterPrinting(int width);
    void clearCells(int y, int fromX, int toX, const Cell& blank);
    void clearRows(int fromY, int toYExclusive, const Cell& blank);
    int beginCharacterEditAtCursor(int count);
    int findTabStop(int from, int step, int limit) const;
    void shrinkPrimaryGrid(int newRows);
    void growPrimaryGrid(int newRows);
    void resizeAlternateGrid(int newRows);
    void reflowPrimaryScreen(int newRows);
    void resizeLines(std::vector<Line>& lines);
    void resizeTabStops(const std::vector<bool>& oldStops);
    void scrollUpRegion(int count, int top, int bottom);
    void scrollDownRegion(int count, int top, int bottom);
    void newlineWithScroll();
    template <typename Visit>
    void forEveryCell(Visit&& visit);

    const Theme& theme;
    int cols = 0;
    int rowCount = 0;
    std::vector<Line> grid;
    std::vector<Line> altGrid;
    std::deque<Line> scrollback;
    CursorPos savedCursor;
    Pen savedPen;
    int scrollTop = 0;
    int scrollBottom = 0;
    bool wrapPending = false;
    char32_t lastPrinted = 0;
    std::vector<bool> tabStops;
    Pen pen;
    HyperlinkTable hyperlinks;
    StyleTable styles;
    GraphemeTable graphemes;

    // The pen's interned styles, for printed and erased cells; refreshed when
    // the pen or the tables change.
    StyleId penStyle = 0;
    StyleId blankStyle = 0;
    bool penStylesDirty = true;
    KittyKeyboardStack primaryKittyKeyboard;
    KittyKeyboardStack alternateKittyKeyboard;
    std::uint64_t changeVersion = 1;
    std::vector<bool> dirtyRows;
};
} // namespace term
