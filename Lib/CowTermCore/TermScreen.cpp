#include "TermScreen.h"

#include <algorithm>
#include <limits>

namespace term
{
namespace
{
constexpr int maxScrollback = 10000;
constexpr int tabStopInterval = 8;
constexpr int smallestColumnCount = 2;
constexpr int wideCharacterWidth = 2;

constexpr int eraseBelowCursor = 0;
constexpr int eraseAboveCursor = 1;
constexpr int eraseWholeScreen = 2;
constexpr int eraseScreenAndScrollback = 3;
constexpr int eraseToLineEnd = 0;
constexpr int eraseToLineStart = 1;
constexpr int clearTabStopAtCursor = 0;
constexpr int clearAllTabStops = 3;

bool isDefaultTabStop(int column)
{
    return column > 0 && column % tabStopInterval == 0;
}
} // namespace

TermScreen::TermScreen(int colsToUse, int rowsToUse, const Theme& themeToUse)
    : theme(themeToUse)
{
    pen.fg = theme.foreground;
    pen.bg = theme.background;
    resize(std::max(colsToUse, 1), std::max(rowsToUse, 1));
}

std::uint16_t TermScreen::defaultColorAttributes() const
{
    const auto foregroundBit = pen.defaultFg ? Attr::DefaultFg : 0;
    const auto backgroundBit = pen.defaultBg ? Attr::DefaultBg : 0;
    return (std::uint16_t) (foregroundBit | backgroundBit);
}

Cell TermScreen::blankCell() const
{
    auto cell = Cell {};
    cell.fg = pen.defaultFg ? theme.foreground : pen.fg;
    cell.bg = pen.defaultBg ? theme.background : pen.bg;
    cell.attrs = defaultColorAttributes();
    return cell;
}

Cell TermScreen::penCell(char32_t codepoint) const
{
    auto cell = blankCell();
    cell.ch = codepoint;
    cell.attrs = (std::uint16_t) (pen.attrs | defaultColorAttributes());
    cell.hyperlink = pen.hyperlink;
    return cell;
}

KittyKeyboardStack& TermScreen::kittyKeyboard()
{
    return modes.altScreen ? alternateKittyKeyboard : primaryKittyKeyboard;
}

const KittyKeyboardStack& TermScreen::kittyKeyboard() const
{
    return modes.altScreen ? alternateKittyKeyboard : primaryKittyKeyboard;
}

void TermScreen::setHyperlink(std::string_view uri)
{
    if (uri.empty())
    {
        pen.hyperlink = noHyperlink;
        return;
    }

    if (hyperlinks.isFull())
        dropUnusedHyperlinks();

    pen.hyperlink = hyperlinks.intern(uri);
}

template <typename Visit>
void TermScreen::forEveryCell(Visit&& visit)
{
    auto visitLine = [&](Line& line) { std::for_each(line.begin(), line.end(), visit); };

    std::for_each(grid.begin(), grid.end(), visitLine);
    std::for_each(altGrid.begin(), altGrid.end(), visitLine);
    std::for_each(scrollback.begin(), scrollback.end(), visitLine);
}

void TermScreen::dropUnusedHyperlinks()
{
    constexpr auto idCount = std::numeric_limits<HyperlinkId>::max() + std::size_t {1};
    auto isUsed = std::vector<bool>(idCount);

    forEveryCell([&](const Cell& cell) { isUsed[cell.hyperlink] = true; });
    isUsed[savedPen.hyperlink] = true;

    const auto newIds = hyperlinks.keepOnly(isUsed);

    forEveryCell([&](Cell& cell) { cell.hyperlink = newIds[cell.hyperlink]; });
    savedPen.hyperlink = newIds[savedPen.hyperlink];
}

Line TermScreen::blankLine() const
{
    auto line = Line {};
    line.assign((std::size_t) cols, blankCell());
    return line;
}

void TermScreen::clampCursor()
{
    cursor.x = std::clamp(cursor.x, 0, cols - 1);
    cursor.y = std::clamp(cursor.y, 0, rowCount - 1);
}

void TermScreen::damageWidePair(int x, int y)
{
    auto& line = row(y);
    auto& cell = line[(std::size_t) x];

    if ((cell.attrs & Attr::WideCont) != 0 && x > 0)
    {
        auto& lead = line[(std::size_t) x - 1];
        lead.ch = U' ';
        lead.attrs &= (std::uint16_t) ~Attr::Wide;
    }

    if ((cell.attrs & Attr::Wide) != 0 && x + 1 < cols)
    {
        auto& cont = line[(std::size_t) x + 1];
        cont.ch = U' ';
        cont.attrs &= (std::uint16_t) ~Attr::WideCont;
    }
}

bool TermScreen::isOnScreen(int x, int y) const
{
    return x >= 0 && x < cols && y >= 0 && y < rowCount;
}

void TermScreen::writeCell(int x, int y, const Cell& cell)
{
    if (!isOnScreen(x, y))
        return;

    damageWidePair(x, y);
    row(y)[(std::size_t) x] = cell;
}

void TermScreen::newlineWithScroll()
{
    carriageReturn();
    linefeed();
}

void TermScreen::wrapIfPending()
{
    if (!wrapPending || !modes.autowrap)
        return;

    newlineWithScroll();
    wrapPending = false;
}

// A wide char that no longer fits on the line wraps early.
void TermScreen::makeRoomForWideCharacter()
{
    if (cursor.x < cols - 1)
        return;

    if (modes.autowrap)
        newlineWithScroll();
    else
        cursor.x = std::max(cols - wideCharacterWidth, 0);
}

void TermScreen::writeWideCell(Cell cell)
{
    cell.attrs |= Attr::Wide;
    writeCell(cursor.x, cursor.y, cell);

    auto continuation = cell;
    continuation.ch = U' ';
    continuation.attrs = (std::uint16_t) ((cell.attrs & ~Attr::Wide) | Attr::WideCont);
    writeCell(cursor.x + 1, cursor.y, continuation);
}

void TermScreen::advanceAfterPrinting(int width)
{
    cursor.x += width;

    if (cursor.x >= cols)
    {
        cursor.x = cols - 1;
        wrapPending = modes.autowrap;
    }
}

void TermScreen::putChar(char32_t cp)
{
    const auto width = charWidth(cp);

    if (width == 0)
        return;

    wrapIfPending();

    if (width == wideCharacterWidth)
    {
        makeRoomForWideCharacter();
        writeWideCell(penCell(cp));
    }
    else
    {
        writeCell(cursor.x, cursor.y, penCell(cp));
    }

    lastPrinted = cp;
    advanceAfterPrinting(width);
    markDirty();
}

void TermScreen::repeatLastChar(int count)
{
    if (lastPrinted == 0)
        return;

    for (auto i = 0; i < std::min(count, cols * rowCount); ++i)
        putChar(lastPrinted);
}

void TermScreen::linefeed()
{
    wrapPending = false;

    if (cursor.y == scrollBottom)
        scrollUpRegion(1, scrollTop, scrollBottom);
    else if (cursor.y < rowCount - 1)
        ++cursor.y;

    markDirty();
}

void TermScreen::carriageReturn()
{
    cursor.x = 0;
    wrapPending = false;
    markDirty();
}

void TermScreen::backspace()
{
    wrapPending = false;

    if (cursor.x > 0)
        --cursor.x;

    markDirty();
}

// Steps from `from` towards `limit` until a tab stop or the limit itself.
int TermScreen::findTabStop(int from, int step, int limit) const
{
    auto x = from;

    while (x != limit && !tabStops[(std::size_t) x])
        x += step;

    return x;
}

void TermScreen::horizontalTab(int count)
{
    wrapPending = false;
    const auto lastColumn = cols - 1;

    for (auto i = 0; i < count; ++i)
        cursor.x = findTabStop(std::min(cursor.x + 1, lastColumn), 1, lastColumn);

    markDirty();
}

void TermScreen::backTab(int count)
{
    wrapPending = false;

    for (auto i = 0; i < count; ++i)
        cursor.x = findTabStop(std::max(cursor.x - 1, 0), -1, 0);

    markDirty();
}

void TermScreen::reverseIndex()
{
    wrapPending = false;

    if (cursor.y == scrollTop)
        scrollDownRegion(1, scrollTop, scrollBottom);
    else if (cursor.y > 0)
        --cursor.y;

    markDirty();
}

void TermScreen::setTabStop()
{
    tabStops[(std::size_t) cursor.x] = true;
}

void TermScreen::clearTabStops(int mode)
{
    if (mode == clearTabStopAtCursor)
        tabStops[(std::size_t) cursor.x] = false;
    else if (mode == clearAllTabStops)
        std::fill(tabStops.begin(), tabStops.end(), false);
}

void TermScreen::setCursor(int col, int row)
{
    wrapPending = false;
    cursor.x = col;
    cursor.y = modes.origin ? scrollTop + row : row;

    if (modes.origin)
        cursor.y = std::clamp(cursor.y, scrollTop, scrollBottom);

    clampCursor();
    markDirty();
}

void TermScreen::setColumn(int col)
{
    wrapPending = false;
    cursor.x = std::clamp(col, 0, cols - 1);
    markDirty();
}

void TermScreen::setRow(int rowToUse)
{
    setCursor(cursor.x, rowToUse);
}

void TermScreen::moveCursor(int dx, int dy)
{
    wrapPending = false;
    cursor.x = std::clamp(cursor.x + dx, 0, cols - 1);

    // Relative vertical motion stays inside the scroll region when the cursor
    // starts inside it, matching DEC semantics.
    const auto top = cursor.y >= scrollTop ? scrollTop : 0;
    const auto bottom = cursor.y <= scrollBottom ? scrollBottom : rowCount - 1;
    cursor.y = std::clamp(cursor.y + dy, top, bottom);
    markDirty();
}

void TermScreen::saveCursor()
{
    savedCursor = cursor;
    savedPen = pen;
}

void TermScreen::restoreCursor()
{
    cursor = savedCursor;
    pen = savedPen;
    wrapPending = false;
    clampCursor();
    markDirty();
}

void TermScreen::clearCells(int y, int fromX, int toX, const Cell& blank)
{
    auto& line = row(y);

    for (auto x = std::max(fromX, 0); x <= std::min(toX, cols - 1); ++x)
        line[(std::size_t) x] = blank;
}

void TermScreen::clearRows(int fromY, int toYExclusive, const Cell& blank)
{
    for (auto y = fromY; y < toYExclusive; ++y)
        clearCells(y, 0, cols - 1, blank);
}

void TermScreen::eraseInDisplay(int mode)
{
    wrapPending = false;

    if (mode == eraseScreenAndScrollback)
    {
        scrollback.clear();
        eraseInDisplay(eraseWholeScreen);
        return;
    }

    const auto blank = blankCell();

    if (mode == eraseBelowCursor)
    {
        clearCells(cursor.y, cursor.x, cols - 1, blank);
        clearRows(cursor.y + 1, rowCount, blank);
    }
    else if (mode == eraseAboveCursor)
    {
        clearRows(0, cursor.y, blank);
        clearCells(cursor.y, 0, cursor.x, blank);
    }
    else if (mode == eraseWholeScreen)
    {
        clearRows(0, rowCount, blank);
    }

    markDirty();
}

void TermScreen::eraseInLine(int mode)
{
    wrapPending = false;
    const auto from = mode == eraseToLineEnd ? cursor.x : 0;
    const auto to = mode == eraseToLineStart ? cursor.x : cols - 1;
    clearCells(cursor.y, from, to, blankCell());
    markDirty();
}

void TermScreen::insertLines(int count)
{
    if (cursor.y < scrollTop || cursor.y > scrollBottom)
        return;

    scrollDownRegion(count, cursor.y, scrollBottom);
    cursor.x = 0;
    markDirty();
}

void TermScreen::deleteLines(int count)
{
    if (cursor.y < scrollTop || cursor.y > scrollBottom)
        return;

    scrollUpRegion(count, cursor.y, scrollBottom);
    cursor.x = 0;
    markDirty();
}

int TermScreen::beginCharacterEditAtCursor(int count)
{
    wrapPending = false;
    damageWidePair(cursor.x, cursor.y);
    return std::clamp(count, 1, cols - cursor.x);
}

void TermScreen::insertChars(int count)
{
    const auto n = beginCharacterEditAtCursor(count);
    auto& line = row(cursor.y);
    line.insert(line.begin() + cursor.x, (std::size_t) n, blankCell());
    line.resize((std::size_t) cols);
    markDirty();
}

void TermScreen::deleteChars(int count)
{
    const auto n = beginCharacterEditAtCursor(count);
    auto& line = row(cursor.y);
    line.erase(line.begin() + cursor.x, line.begin() + cursor.x + n);
    line.resize((std::size_t) cols, blankCell());
    markDirty();
}

void TermScreen::eraseChars(int count)
{
    wrapPending = false;
    auto& line = row(cursor.y);
    const auto n = std::clamp(count, 1, cols - cursor.x);
    const auto blank = blankCell();

    for (auto x = cursor.x; x < cursor.x + n; ++x)
    {
        damageWidePair(x, cursor.y);
        line[(std::size_t) x] = blank;
    }

    markDirty();
}

void TermScreen::scrollUp(int count)
{
    scrollUpRegion(count, scrollTop, scrollBottom);
}

void TermScreen::scrollDown(int count)
{
    scrollDownRegion(count, scrollTop, scrollBottom);
}

bool TermScreen::isScrollingIntoHistory(int top) const
{
    return top == 0 && !modes.altScreen;
}

void TermScreen::scrollUpRegion(int count, int top, int bottom)
{
    const auto n = std::clamp(count, 1, bottom - top + 1);
    auto& lines = activeGrid();

    for (auto i = 0; i < n; ++i)
    {
        // Only lines leaving the top of a full-width primary screen become
        // history; region scrolls and the alt screen just discard.
        if (isScrollingIntoHistory(top))
        {
            scrollback.push_back(std::move(lines[(std::size_t) top]));

            if ((int) scrollback.size() > maxScrollback)
                scrollback.pop_front();
        }

        lines.erase(lines.begin() + top);
        lines.insert(lines.begin() + bottom, blankLine());
    }

    markDirty();
}

void TermScreen::scrollDownRegion(int count, int top, int bottom)
{
    const auto n = std::clamp(count, 1, bottom - top + 1);
    auto& lines = activeGrid();

    for (auto i = 0; i < n; ++i)
    {
        lines.erase(lines.begin() + bottom);
        lines.insert(lines.begin() + top, blankLine());
    }

    markDirty();
}

void TermScreen::setScrollRegion(int top, int bottom)
{
    if (bottom <= top)
    {
        scrollTop = 0;
        scrollBottom = rowCount - 1;
    }
    else
    {
        scrollTop = std::clamp(top, 0, rowCount - 1);
        scrollBottom = std::clamp(bottom, scrollTop, rowCount - 1);
    }

    setCursor(0, 0);
}

void TermScreen::useAltScreen(bool on, bool saveCursorAndClear)
{
    if (on == modes.altScreen)
        return;

    if (on)
    {
        if (saveCursorAndClear)
            saveCursor();

        modes.altScreen = true;

        if (saveCursorAndClear)
        {
            for (auto& line: altGrid)
                std::fill(line.begin(), line.end(), blankCell());

            cursor = {0, 0};
        }
    }
    else
    {
        modes.altScreen = false;

        if (saveCursorAndClear)
            restoreCursor();
    }

    wrapPending = false;
    markDirty();
}

void TermScreen::fullReset()
{
    pen = Pen {};
    pen.fg = theme.foreground;
    pen.bg = theme.background;
    modes = Modes {};
    cursor = {0, 0};
    savedCursor = {0, 0};
    savedPen = pen;
    primaryKittyKeyboard.clear();
    alternateKittyKeyboard.clear();
    wrapPending = false;
    scrollTop = 0;
    scrollBottom = rowCount - 1;

    for (auto& line: grid)
        std::fill(line.begin(), line.end(), blankCell());

    for (auto& line: altGrid)
        std::fill(line.begin(), line.end(), blankCell());

    resizeTabStops({});
    markDirty();
}

bool TermScreen::isAlreadySize(int newCols, int newRows) const
{
    return newCols == cols && newRows == rowCount && !grid.empty();
}

// Overflow above the cursor becomes history so content is not lost.
void TermScreen::shrinkPrimaryGrid(int newRows)
{
    while ((int) grid.size() > newRows)
    {
        if (cursor.y > 0)
        {
            scrollback.push_back(std::move(grid.front()));
            grid.erase(grid.begin());
            --cursor.y;
        }
        else
        {
            grid.pop_back();
        }
    }
}

// History comes back onto the screen before blank lines are added.
void TermScreen::growPrimaryGrid(int newRows)
{
    while ((int) grid.size() < newRows)
    {
        if (!scrollback.empty())
        {
            grid.insert(grid.begin(), std::move(scrollback.back()));
            scrollback.pop_back();
            ++cursor.y;
        }
        else
        {
            grid.emplace_back();
        }
    }
}

void TermScreen::resizeAlternateGrid(int newRows)
{
    altGrid.resize((std::size_t) newRows);
}

void TermScreen::resizeLines(std::vector<Line>& lines)
{
    for (auto& line: lines)
        line.resize((std::size_t) cols, blankCell());
}

// Columns that existed keep their stop; new columns get the default every
// eighth column.
void TermScreen::resizeTabStops(const std::vector<bool>& oldStops)
{
    const auto wasTabStop = [&](int x)
    {
        if (x < (int) oldStops.size())
            return x > 0 && oldStops[(std::size_t) x];

        return isDefaultTabStop(x);
    };

    tabStops.assign((std::size_t) cols, false);

    for (auto x = 0; x < cols; ++x)
        tabStops[(std::size_t) x] = wasTabStop(x);
}

void TermScreen::resize(int newCols, int newRows)
{
    newCols = std::max(newCols, smallestColumnCount);
    newRows = std::max(newRows, 1);

    if (isAlreadySize(newCols, newRows))
        return;

    cols = newCols;
    shrinkPrimaryGrid(newRows);
    growPrimaryGrid(newRows);
    resizeLines(grid);
    resizeAlternateGrid(newRows);
    resizeLines(altGrid);

    rowCount = newRows;
    scrollTop = 0;
    scrollBottom = rowCount - 1;
    wrapPending = false;

    resizeTabStops(std::vector<bool> {tabStops});
    clampCursor();
    markDirty();
}

const Line& TermScreen::lineAt(int visualRow, int scrollOffset) const
{
    const auto& lines = activeGrid();
    const auto index = (std::int64_t) scrollback.size() - scrollOffset + visualRow;

    if (index < 0)
        return lines[0];

    if (index < (std::int64_t) scrollback.size())
        return scrollback[(std::size_t) index];

    const auto gridIndex =
        std::min((std::size_t) (index - scrollback.size()), lines.size() - 1);
    return lines[gridIndex];
}
} // namespace term
