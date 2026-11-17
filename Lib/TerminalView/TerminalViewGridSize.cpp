#include "TerminalView.h"

#include "Emulator/Debug.h"

#include <algorithm>
#include <cmath>

namespace term
{
using namespace eacp;

namespace
{
constexpr auto minimumGridColumns = 2;
constexpr auto minimumGridRows = 1;

// How many whole cells fit along a length after a margin at each end.
int countWholeCellsThatFit(float length, float margin, float cellSize)
{
    const auto cellsThatFit = std::floor((length - 2 * margin) / cellSize);
    const auto wholeCells = static_cast<int>(cellsThatFit);
    return wholeCells;
}
} // namespace

void TerminalView::applyGridSize()
{
    const auto bounds = getLocalBounds();
    const auto cellW = atlas->cellWidth();
    const auto cellH = atlas->cellHeight();

    if (!isPositiveSize(cellW, cellH) || !isPositiveSize(bounds.w, bounds.h))
        return;

    const auto size = fixedGridSize.value_or(calculateGridSizeFromBounds());
    const auto cols = size.cols;
    const auto rows = size.rows;
    hasGridSizeFromBounds = true;

    if (commandTerminal)
        debugLog("[grid] popup bounds w=%.1f h=%.1f cell=%.2fx%.2f font=%.1f -> %dx%d\n",
                 bounds.w, bounds.h, cellW, cellH, fontSize, cols, rows);

    if (cols == screen.columns() && rows == screen.rows())
        return;

    // A resize reflows and renumbers every row, so the view keeps its distance
    // from the bottom instead.
    syncScrollPositionWithScrollback();
    const auto linesFromBottom = calculateLinesFromBottom();
    screen.resize(cols, rows);
    shell->resize({cols, rows});
    syncScrollPositionWithScrollback();

    if (!scrollPosition.isFollowingLiveBottom())
        scrollPosition.showLinesFromBottom(linesFromBottom, calculateScrollRange());
}

PtySize TerminalView::calculateGridSizeFromBounds() const
{
    const auto bounds = getLocalBounds();
    const auto cols = countWholeCellsThatFit(bounds.w, marginX, atlas->cellWidth());
    const auto rows = countWholeCellsThatFit(bounds.h, marginY, atlas->cellHeight());
    return {std::max(minimumGridColumns, cols), std::max(minimumGridRows, rows)};
}

// With a fixed grid, the rows the view's height holds; otherwise the grid's.
int TerminalView::countViewportRows() const
{
    if (!fixedGridSize)
        return screen.rows();

    const auto bounds = getLocalBounds();
    const auto rows = countWholeCellsThatFit(bounds.h, marginY, atlas->cellHeight());
    return std::max(minimumGridRows, rows);
}

void TerminalView::setFixedGridSize(std::optional<PtySize> size)
{
    const auto lock = lockTerminal();
    fixedGridSize = size;
    applyGridSize();
    rowDrawings.invalidateAllRows();
    repaintIfShown();
}
} // namespace term
