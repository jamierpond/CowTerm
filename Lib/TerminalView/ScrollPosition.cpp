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

void ScrollPosition::followLiveBottom()
{
    followsLiveBottom = true;
    hiddenFractionOfTopRow = 0.0;
    linesArrivedWhileScrolledBack = 0;
}

ScrollEdgeContact ScrollPosition::scrollByRows(double rows, int scrollbackSize)
{
    const auto bottomPosition = (double) scrollbackSize;
    const auto currentPosition =
        (double) findTopAbsoluteRow(scrollbackSize) + hiddenFractionOfTopRow;
    const auto wantedPosition = currentPosition - rows;
    const auto reachablePosition = std::clamp(wantedPosition, 0.0, bottomPosition);

    moveToRowPosition(reachablePosition, scrollbackSize);

    if (reachablePosition != wantedPosition)
        return ScrollEdgeContact::StoppedAtEdge;

    return ScrollEdgeContact::None;
}

void ScrollPosition::showLinesFromBottom(int linesFromBottom, int scrollbackSize)
{
    const auto clampedLines = std::clamp(linesFromBottom, 0, scrollbackSize);
    const auto rowPosition = (double) (scrollbackSize - clampedLines);
    moveToRowPosition(rowPosition, scrollbackSize);
}

void ScrollPosition::moveToRowPosition(double rowPosition, int scrollbackSize)
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

    const auto row = (long) wholeRows;

    if (row >= scrollbackSize)
    {
        followLiveBottom();
        return;
    }

    if (followsLiveBottom)
        linesArrivedWhileScrolledBack = 0;

    followsLiveBottom = false;
    topRow = row;
    hiddenFractionOfTopRow = fraction;
}

void ScrollPosition::applyScrollbackTotals(ScrollbackTotals totals, int scrollbackSize)
{
    const auto linesAdded = totals.linesAdded - totalsSeen.linesAdded;
    const auto linesDropped = (long) (totals.linesDropped - totalsSeen.linesDropped);
    totalsSeen = totals;

    if (followsLiveBottom)
        return;

    linesArrivedWhileScrolledBack += linesAdded;
    topRow -= linesDropped;

    if (topRow < 0)
    {
        topRow = 0;
        hiddenFractionOfTopRow = 0.0;
    }

    if (topRow >= scrollbackSize)
        followLiveBottom();
}

long ScrollPosition::findTopAbsoluteRow(int scrollbackSize) const
{
    if (followsLiveBottom)
        return scrollbackSize;

    return std::min(topRow, (long) scrollbackSize);
}

int ScrollPosition::calculateLinesFromBottom(int scrollbackSize) const
{
    return (int) (scrollbackSize - findTopAbsoluteRow(scrollbackSize));
}
} // namespace term
