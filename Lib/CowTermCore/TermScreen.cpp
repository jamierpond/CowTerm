#include "TermScreen.h"
#include "GraphemeBreak.h"
#include "Reflow.h"
#include "SelectGraphicRendition.h"

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

StyleId TermScreen::internStyle(const CellStyle& style)
{
    if (style == CellStyle {theme.foreground, theme.background})
        return 0;

    return (StyleId) styles.intern(style);
}

void TermScreen::refreshPenStyles()
{
    if (!penStylesDirty)
        return;

    constexpr auto penStyleCount = std::size_t {2};

    if (!styles.hasRoomFor(penStyleCount))
        compactTables();

    penStylesDirty = false;
    const auto fg = pen.defaultFg ? theme.foreground : pen.fg;
    const auto bg = pen.defaultBg ? theme.background : pen.bg;
    blankStyle = internStyle({fg, bg, defaultColorAttributes()});
    penStyle = internStyle({fg, bg, (std::uint16_t) (pen.attrs | defaultColorAttributes())});
}

const CellStyle& TermScreen::findStyle(StyleId id) const
{
    static const auto unknown = CellStyle {};

    if (styles.holds(id))
        return styles.find(id);

    return unknown;
}

Cell TermScreen::blankCell()
{
    refreshPenStyles();
    auto cell = Cell {};
    cell.style = blankStyle;
    return cell;
}

Cell TermScreen::penCell(char32_t codepoint)
{
    refreshPenStyles();
    auto cell = Cell {};
    cell.setCodepoint(codepoint);
    cell.style = penStyle;
    cell.hyperlink = pen.hyperlink;
    return cell;
}

void TermScreen::applyGraphicRendition(const CsiParameters& parameters)
{
    applySelectGraphicRendition(pen, parameters, theme);
    penStylesDirty = true;
}

std::string_view TermScreen::findHyperlinkUri(HyperlinkId id) const
{
    if (!hyperlinks.holds(id))
        return {};

    return hyperlinks.find(id);
}

std::u32string TermScreen::findClusterText(const Cell& cell) const
{
    if (cell.isGrapheme() && graphemes.holds(cell.value()))
        return graphemes.find(cell.value());

    return std::u32string(1, (char32_t) cell.value());
}

ResolvedCell TermScreen::resolveCell(const Cell& cell) const
{
    const auto defaultStyle = CellStyle {theme.foreground, theme.background};
    const auto& style = cell.style == 0 ? defaultStyle : findStyle(cell.style);
    auto resolved = ResolvedCell {};
    resolved.fg = style.fg;
    resolved.bg = style.bg;
    resolved.attrs = style.attrs;
    resolved.hyperlink = cell.hyperlink;
    resolved.ch = (char32_t) cell.value();

    if (cell.isGrapheme() && graphemes.holds(cell.value()))
    {
        resolved.text = graphemes.find(cell.value());
        resolved.ch = resolved.text.front();
    }

    if (cell.hasFlag(CellFlag::Wide))
        resolved.attrs |= Attr::Wide;

    if (cell.hasFlag(CellFlag::WideCont))
        resolved.attrs |= Attr::WideCont;

    if (cell.hasFlag(CellFlag::WrapSpacer))
        resolved.attrs |= Attr::WrapSpacer;

    return resolved;
}

std::vector<ResolvedCell> TermScreen::resolveLine(const Line& line) const
{
    auto resolved = std::vector<ResolvedCell> {};
    resolved.reserve(line.size());

    for (const auto& cell: line)
        resolved.push_back(resolveCell(cell));

    return resolved;
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
        compactTables();

    pen.hyperlink = (HyperlinkId) hyperlinks.intern(std::string {uri});
}

template <typename Visit>
void TermScreen::forEveryCell(Visit&& visit)
{
    auto visitLine = [&](Line& line) { std::for_each(line.begin(), line.end(), visit); };

    std::for_each(grid.begin(), grid.end(), visitLine);
    std::for_each(altGrid.begin(), altGrid.end(), visitLine);
    std::for_each(scrollback.begin(), scrollback.end(), visitLine);
}

// Ghostty frees styles, links and clusters by reference count; here a full
// table triggers one scan of every cell, which keeps what is still in use.
void TermScreen::compactTables()
{
    auto stylesUsed = std::vector<bool>(styles.size() + 1);
    auto linksUsed = std::vector<bool>(hyperlinks.size() + 1);
    auto clustersUsed = std::vector<bool>(graphemes.size() + 1);

    forEveryCell(
        [&](const Cell& cell)
        {
            stylesUsed[cell.style] = true;
            linksUsed[cell.hyperlink] = true;

            if (cell.isGrapheme())
                clustersUsed[cell.value()] = true;
        });

    linksUsed[savedPen.hyperlink] = true;
    linksUsed[pen.hyperlink] = true;

    const auto newStyles = styles.keepOnly(stylesUsed);
    const auto newLinks = hyperlinks.keepOnly(linksUsed);
    const auto newClusters = graphemes.keepOnly(clustersUsed);

    forEveryCell(
        [&](Cell& cell)
        {
            cell.style = (StyleId) newStyles[cell.style];
            cell.hyperlink = (HyperlinkId) newLinks[cell.hyperlink];

            if (cell.isGrapheme())
                cell.content = (cell.content & ~Cell::valueMask) | newClusters[cell.value()];
        });

    savedPen.hyperlink = (HyperlinkId) newLinks[savedPen.hyperlink];
    pen.hyperlink = (HyperlinkId) newLinks[pen.hyperlink];
    penStylesDirty = true;
}

Line TermScreen::blankLine()
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

    if (cell.hasFlag(CellFlag::WideCont) && x > 0)
    {
        auto& lead = line[(std::size_t) x - 1];
        lead.setCodepoint(U' ');
        lead.setFlag(CellFlag::Wide, false);
    }

    if (cell.hasFlag(CellFlag::Wide) && x + 1 < cols)
    {
        auto& cont = line[(std::size_t) x + 1];
        cont.setCodepoint(U' ');
        cont.setFlag(CellFlag::WideCont, false);
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

    row(cursor.y).wrapped = true;
    newlineWithScroll();
    wrapPending = false;
}

// A wide char that no longer fits on the line wraps early.
void TermScreen::makeRoomForWideCharacter()
{
    if (cursor.x < cols - 1)
        return;

    if (modes.autowrap)
    {
        auto spacer = blankCell();
        spacer.setFlag(CellFlag::WrapSpacer, true);
        writeCell(cursor.x, cursor.y, spacer);
        row(cursor.y).wrapped = true;
        newlineWithScroll();
    }
    else
        cursor.x = std::max(cols - wideCharacterWidth, 0);
}

void TermScreen::writeWideCell(Cell cell)
{
    cell.setFlag(CellFlag::Wide, true);
    writeCell(cursor.x, cursor.y, cell);

    auto continuation = cell;
    continuation.setCodepoint(U' ');
    continuation.setFlag(CellFlag::Wide, false);
    continuation.setFlag(CellFlag::WideCont, true);
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

// The cell a codepoint would extend: the last one printed, left of the
// cursor (or under it with a wrap pending), if the break rules join them.
std::optional<TermScreen::ClusterTarget> TermScreen::findClusterToExtend(char32_t codepoint) const
{
    auto target = ClusterTarget {wrapPending ? cursor.x : cursor.x - 1, cursor.y};
    const auto& line = activeGrid()[(std::size_t) target.y];

    if (target.x >= 0 && line[(std::size_t) target.x].hasFlag(CellFlag::WideCont))
        --target.x;

    if (target.x < 0)
        return std::nullopt;

    const auto& cell = line[(std::size_t) target.x];

    if (!isClusterExtender(codepoint) && !cell.isGrapheme())
        return std::nullopt;

    if (!doesExtendCluster(findClusterText(cell), codepoint))
        return std::nullopt;

    return target;
}

void TermScreen::extendCluster(ClusterTarget target, char32_t codepoint)
{
    if (graphemes.isFull())
        compactTables();

    auto& cell = row(target.y)[(std::size_t) target.x];
    const auto id = graphemes.intern(findClusterText(cell) + codepoint);

    if (id == GraphemeTable::none)
        return;

    cell.content = (cell.content & ~Cell::valueMask) | id | CellFlag::Grapheme;

    if (codepoint == variationSelectorEmoji)
        widenForEmojiPresentation(target);
}

// VS16 asks for the emoji form, which is two cells wide (Ghostty does the
// same): widen a narrow cell when the cell after it is free.
void TermScreen::widenForEmojiPresentation(ClusterTarget target)
{
    auto& line = row(target.y);
    const auto isNextCellFree = !wrapPending && target.x + 1 == cursor.x && cursor.x < cols;

    if (line[(std::size_t) target.x].hasFlag(CellFlag::Wide) || !isNextCellFree)
        return;

    line[(std::size_t) target.x].setFlag(CellFlag::Wide, true);
    auto continuation = line[(std::size_t) target.x];
    continuation.setCodepoint(U' ');
    continuation.setFlag(CellFlag::Wide, false);
    continuation.setFlag(CellFlag::WideCont, true);
    writeCell(cursor.x, cursor.y, continuation);
    advanceAfterPrinting(1);
}

void TermScreen::putChar(char32_t cp)
{
    constexpr auto firstCodepointThatCanJoin = char32_t {0x0300};
    const auto mayJoinACluster = cp >= firstCodepointThatCanJoin;

    if (const auto target = mayJoinACluster ? findClusterToExtend(cp) : std::nullopt)
    {
        extendCluster(*target, cp);
        markDirty();
        return;
    }

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
    penStylesDirty = true;
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
    {
        clearCells(y, 0, cols - 1, blank);
        row(y).wrapped = false;
    }
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

    // Erasing to the end means the row no longer runs on (Ghostty, EL 0/2).
    if (mode != eraseToLineStart)
        row(cursor.y).wrapped = false;

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
    line.wrapped = false;
    line.insert(line.begin() + cursor.x, (std::size_t) n, blankCell());
    line.resize((std::size_t) cols);
    markDirty();
}

void TermScreen::deleteChars(int count)
{
    const auto n = beginCharacterEditAtCursor(count);
    auto& line = row(cursor.y);
    line.wrapped = false;
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
    penStylesDirty = true;
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

// Rewraps the scrollback and screen at the new width. The cursor's row stays
// on screen; rows above the new screen become history.
void TermScreen::reflowPrimaryScreen(int newRows)
{
    auto rows = std::vector<Line>(std::make_move_iterator(scrollback.begin()),
                                  std::make_move_iterator(scrollback.end()));
    const auto cursorRow = (long) rows.size() + cursor.y;
    std::move(grid.begin(), grid.end(), std::back_inserter(rows));
    scrollback.clear();
    grid.clear();

    const auto blank = blankCell();
    auto reflowed = reflowLines({rows, cursorRow, cursor.x, cols, blank});
    auto& lines = reflowed.lines;

    while ((long) lines.size() < reflowed.cursorRow + 1 || (int) lines.size() < newRows)
        lines.emplace_back((std::size_t) cols, blank);

    const auto lastStart = (long) lines.size() - newRows;
    const auto gridStart = std::min(lastStart, reflowed.cursorRow);
    const auto historyStart = std::max(gridStart - maxScrollback, 0L);

    scrollback.assign(std::make_move_iterator(lines.begin() + historyStart),
                      std::make_move_iterator(lines.begin() + gridStart));
    grid.assign(std::make_move_iterator(lines.begin() + gridStart),
                std::make_move_iterator(lines.begin() + gridStart + newRows));
    cursor.y = (int) (reflowed.cursorRow - gridStart);
    cursor.x = reflowed.cursorColumn;
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

    if (newCols != cols && !grid.empty())
    {
        cols = newCols;
        reflowPrimaryScreen(newRows);
    }
    else
    {
        cols = newCols;
        shrinkPrimaryGrid(newRows);
        growPrimaryGrid(newRows);
    }

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
