#include "ScrollPosition.h"

#include <algorithm>
#include <cmath>

namespace term
{
namespace
{
// Below this a part row is float noise from converting pixels to rows, not a
// scroll the reader made.
constexpr auto fractionTreatedAsWholeRow = 1.0e-4;
} // namespace

ScrollRange calculateScrollRange(const ViewportLayout& layout)
{
    const auto rowsBelowViewport = layout.gridRows - layout.viewportRows;
    const auto liveBottomTopRow = layout.scrollbackSize + rowsBelowViewport;

    if (!layout.isScrollbackShown)
        return {liveBottomTopRow, liveBottomTopRow};

    return {std::min(0L, liveBottomTopRow), liveBottomTopRow};
}

void ScrollPosition::followLiveBottom()
{
    setFollowsLiveBottom(true);
    hiddenFractionOfTopRow = 0.0;
    setLinesArrivedWhileScrolledBack(0);
}

void ScrollPosition::setFollowsLiveBottom(bool follows)
{
    if (follows != followsLiveBottom)
        scrolledBackStateChanged = true;

    followsLiveBottom = follows;
}

void ScrollPosition::setLinesArrivedWhileScrolledBack(std::uint64_t count)
{
    if (count != linesArrivedWhileScrolledBack)
        scrolledBackStateChanged = true;

    linesArrivedWhileScrolledBack = count;
}

bool ScrollPosition::takeWhetherScrolledBackStateChanged()
{
    const auto changed = scrolledBackStateChanged;
    scrolledBackStateChanged = false;
    return changed;
}

ScrollEdgeContact ScrollPosition::scrollByRows(double rows, ScrollRange range)
{
    const auto topRowPosition = static_cast<double>(findTopAbsoluteRow(range));
    const auto oldestPosition = static_cast<double>(range.oldestTopRow);
    const auto liveBottomPosition = static_cast<double>(range.liveBottomTopRow);
    const auto wantedPosition = topRowPosition + hiddenFractionOfTopRow - rows;
    const auto reachablePosition =
        std::clamp(wantedPosition, oldestPosition, liveBottomPosition);

    moveToRowPosition(reachablePosition, range);

    if (reachablePosition != wantedPosition)
        return ScrollEdgeContact::StoppedAtEdge;

    return ScrollEdgeContact::None;
}

void ScrollPosition::showLinesFromBottom(int linesFromBottom, ScrollRange range)
{
    showTopRow(range.liveBottomTopRow - linesFromBottom, range);
}

void ScrollPosition::showTopRow(long absoluteRow, ScrollRange range)
{
    const auto clampedRow = std::clamp(absoluteRow, range.oldestTopRow, range.liveBottomTopRow);
    const auto rowPosition = static_cast<double>(clampedRow);
    moveToRowPosition(rowPosition, range);
}

void ScrollPosition::moveToRowPosition(double rowPosition, ScrollRange range)
{
    auto wholeRows = std::floor(rowPosition);
    auto fraction = rowPosition - wholeRows;

    if (fraction < fractionTreatedAsWholeRow)
        fraction = 0.0;

    if (fraction > 1.0 - fractionTreatedAsWholeRow)
    {
        wholeRows += 1.0;
        fraction = 0.0;
    }

    const auto row = static_cast<long>(wholeRows);

    if (row >= range.liveBottomTopRow)
    {
        followLiveBottom();
        return;
    }

    if (followsLiveBottom)
        setLinesArrivedWhileScrolledBack(0);

    setFollowsLiveBottom(false);
    topRow = row;
    hiddenFractionOfTopRow = fraction;
}

void ScrollPosition::applyScrollbackTotals(ScrollbackTotals totals, ScrollRange range)
{
    const auto linesAdded = totals.linesAdded - totalsSeen.linesAdded;
    const auto linesDroppedCount = totals.linesDropped - totalsSeen.linesDropped;
    const auto linesDropped = static_cast<long>(linesDroppedCount);
    totalsSeen = totals;

    if (followsLiveBottom)
        return;

    setLinesArrivedWhileScrolledBack(linesArrivedWhileScrolledBack + linesAdded);
    topRow -= linesDropped;

    if (topRow < range.oldestTopRow)
    {
        topRow = range.oldestTopRow;
        hiddenFractionOfTopRow = 0.0;
    }

    if (topRow >= range.liveBottomTopRow)
        followLiveBottom();
}

long ScrollPosition::findTopAbsoluteRow(ScrollRange range) const
{
    if (followsLiveBottom)
        return range.liveBottomTopRow;

    return std::clamp(topRow, range.oldestTopRow, range.liveBottomTopRow);
}

int ScrollPosition::calculateLinesFromBottom(ScrollRange range) const
{
    const auto linesFromBottom = range.liveBottomTopRow - findTopAbsoluteRow(range);
    return static_cast<int>(linesFromBottom);
}
} // namespace term
