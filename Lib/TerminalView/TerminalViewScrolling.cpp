#include "TerminalView.h"

#include <cmath>
#include <utility>

namespace term
{
using namespace eacp;
using Graphics::MouseEvent;
using Graphics::TouchEvent;

namespace
{
TouchSample makeTouchSample(const TouchEvent& touch)
{
    return {FractionalSeconds {touch.timestamp}, touch.pos.y};
}

// The cell `rows` rows further up, or the buffer's first cell once it would
// be above the top.
CellRef moveCellUpWithinBuffer(CellRef cell, long rows)
{
    if (cell.row < rows)
        return {};

    return {cell.row - rows, cell.col};
}
} // namespace

void TerminalView::syncScrollPositionWithScrollback()
{
    const auto& movement = screen.scrollbackMovement();
    const auto totals = ScrollbackTotals {movement.linesAdded, movement.linesDropped};
    scrollPosition.applyScrollbackTotals(totals, calculateScrollRange());
}

// The output thread calls this, so it never repaints; showParsedOutput does.
void TerminalView::followScrollbackMovement()
{
    syncScrollPositionWithScrollback();
    const auto previous = std::exchange(seenScrollbackMovement, screen.scrollbackMovement());
    const auto& current = seenScrollbackMovement;

    if (current.renumberings != previous.renumberings)
    {
        forgetHeldRows();
        return;
    }

    const auto dropped = (long) (current.linesDropped - previous.linesDropped);

    if (dropped > 0)
        moveHeldRowsUp(dropped);
}

void TerminalView::moveHeldRowsUp(long rows)
{
    const auto isSelectionGone = selection.end.row < rows;

    selection.start = moveCellUpWithinBuffer(selection.start, rows);
    selection.end = moveCellUpWithinBuffer(selection.end, rows);
    selectionAnchor = moveCellUpWithinBuffer(selectionAnchor, rows);
    copyAnchor = moveCellUpWithinBuffer(copyAnchor, rows);
    copyCursor = moveCellUpWithinBuffer(copyCursor, rows);

    if (isSelectionGone)
        selection.active = false;

    rowDrawings.invalidateAllRows();
}

void TerminalView::forgetHeldRows()
{
    selecting = false;
    selection.active = false;
    copySelecting = false;
    copyLineSelect = false;

    if (copyMode)
        placeCopyCursor(copyCursor);

    rowDrawings.invalidateAllRows();
}

void TerminalView::reportScrolledBackStateIfChanged()
{
    if (!scrollPosition.takeWhetherScrolledBackStateChanged())
        return;

    onScrolledBackStateChanged({!scrollPosition.isFollowingLiveBottom(),
                                scrollPosition.countLinesArrivedWhileScrolledBack()});
}

ScrollRange TerminalView::calculateScrollRange() const
{
    return term::calculateScrollRange(
        {screen.scrollbackSize(), screen.rows(), countViewportRows(), !screen.modes.altScreen});
}

// The alternate screen shows blank rows, not the primary screen's history,
// above its grid.
long TerminalView::findFirstShownAbsoluteRow() const
{
    if (screen.modes.altScreen)
        return screen.scrollbackSize();

    return 0;
}

bool TerminalView::isAbsoluteRowShown(long absoluteRow) const
{
    return absoluteRow >= findFirstShownAbsoluteRow();
}

int TerminalView::calculateLinesFromBottom() const
{
    return scrollPosition.calculateLinesFromBottom(calculateScrollRange());
}

// Rounded to whole device pixels, so glyphs land on the pixel grid the atlas
// rasterized them for.
float TerminalView::calculatePixelOffsetIntoTopRow() const
{
    const auto hiddenFraction = static_cast<float>(scrollPosition.getHiddenFractionOfTopRow());
    const auto offsetInPoints = hiddenFraction * atlas->cellHeight();
    const auto devicePixelsPerPoint = atlas->scale();
    return std::round(offsetInPoints * devicePixelsPerPoint) / devicePixelsPerPoint;
}

// A part-scrolled top row lets one more row show at the bottom.
int TerminalView::countRowsToDraw() const
{
    if (calculatePixelOffsetIntoTopRow() > 0.f)
        return countViewportRows() + 1;

    return countViewportRows();
}

std::uint64_t TerminalView::countLinesArrivedWhileScrolledBack() const
{
    const auto lock = lockTerminal();
    return scrollPosition.countLinesArrivedWhileScrolledBack();
}

void TerminalView::scrollToLiveBottom()
{
    const auto lock = lockTerminal();
    stopScrollMomentum();

    if (scrollPosition.isFollowingLiveBottom())
        return;

    scrollPosition.followLiveBottom();
    rowDrawings.invalidateAllRows();
    reportScrolledBackStateIfChanged();
    repaintIfShown();
}

ScrollEdgeContact TerminalView::scrollByRows(double rows)
{
    syncScrollPositionWithScrollback();
    const auto range = calculateScrollRange();
    const auto contact = scrollPosition.scrollByRows(rows, range);

    if (range.liveBottomTopRow > range.oldestTopRow)
    {
        scrollIndicatorFade.showForScroll();
        updateContinuousRendering();
    }

    reportScrolledBackStateIfChanged();
    repaintIfShown();
    return contact;
}

ScrollEdgeContact TerminalView::scrollByPixels(double pixels)
{
    return scrollByRows(pixels / atlas->cellHeight());
}

void TerminalView::scrollBy(int lines)
{
    scrollByRows(lines);
}

void TerminalView::stopScrollMomentum()
{
    scrollMomentum.stop();
    updateContinuousRendering();
}

// Frames run every refresh only while something moves: a coast, or the
// scroll indicator before it has faded.
void TerminalView::updateContinuousRendering()
{
    const auto isAnimating = scrollMomentum.isMoving() || scrollIndicatorFade.isVisible();

    if (isAnimating != isContinuous())
        setContinuous(isAnimating);
}

void TerminalView::update(Threads::FrameTime frameTime)
{
    const auto lock = lockTerminal();
    const auto elapsed = FractionalSeconds {frameTime.delta};

    if (scrollMomentum.isMoving())
    {
        const auto pixels = scrollMomentum.advanceBy(elapsed);

        if (scrollByPixels(pixels) == ScrollEdgeContact::StoppedAtEdge)
            scrollMomentum.stop();
    }

    scrollIndicatorFade.advanceBy(elapsed);
    updateContinuousRendering();
    repaintIfShown();
}

void TerminalView::drawScrollIndicator(GridPainter& painter)
{
    constexpr auto indicatorWidth = 3.f;
    constexpr auto indicatorInset = 3.f;

    const auto range = calculateScrollRange();
    const auto bounds = getLocalBounds();
    const auto rowsFromOldest = findTopVisibleAbsoluteRow() - range.oldestTopRow;
    const auto scrollableRows = range.liveBottomTopRow - range.oldestTopRow;
    const auto layout = ScrollIndicatorLayout {
        indicatorInset,
        bounds.h - 2.f * indicatorInset,
        static_cast<double>(scrollableRows),
        static_cast<double>(countViewportRows()),
        static_cast<double>(rowsFromOldest) + scrollPosition.getHiddenFractionOfTopRow()};
    const auto thumb = calculateScrollIndicatorThumb(layout);
    const auto thumbTop = static_cast<float>(thumb.top);
    const auto thumbHeight = static_cast<float>(thumb.height);
    const auto indicatorOpacity = static_cast<float>(scrollIndicatorFade.calculateOpacity());
    const auto left = bounds.w - indicatorInset - indicatorWidth;

    painter.drawScrollIndicator({left, thumbTop, indicatorWidth, thumbHeight}, indicatorOpacity);
}

void TerminalView::beginTouchScroll(const TouchEvent& touch)
{
    const auto lock = lockTerminal();
    stopScrollMomentum();
    wheelRemainder = 0.f;
    lastTouchPosition = touch.pos;
    touchVelocity.reset();
    touchVelocity.addSample(makeTouchSample(touch));
}

void TerminalView::moveTouchScroll(const TouchEvent& touch)
{
    const auto lock = lockTerminal();
    const auto pixels = touch.pos.y - lastTouchPosition.y;
    lastTouchPosition = touch.pos;
    touchVelocity.addSample(makeTouchSample(touch));

    auto event = MouseEvent {};
    event.pos = touch.pos;
    event.fromTouch = true;

    if (!isWheelForProgram(event))
    {
        scrollByPixels(pixels);
        return;
    }

    const auto lines =
        takeWholeWheelLines(convertWheelDeltaToLines(pixels, true, getPointsPerWheelLine()));

    if (lines != 0)
        sendWheelLinesToProgram(event, lines);
}

void TerminalView::endTouchScroll(const TouchEvent& touch)
{
    const auto liftTime = FractionalSeconds {touch.timestamp};
    endTouchScrollWithVelocity(touchVelocity.calculateVelocityAtLift(liftTime));
}

void TerminalView::endTouchScrollWithVelocity(double velocityPixelsPerSecond)
{
    const auto lock = lockTerminal();
    auto event = MouseEvent {};
    event.pos = lastTouchPosition;
    event.fromTouch = true;
    touchVelocity.reset();

    if (isWheelForProgram(event))
        return;

    scrollMomentum.startFling(velocityPixelsPerSecond);
    updateContinuousRendering();
}

// ---- wheel -----------------------------------------------------------------

int TerminalView::takeWholeWheelLines(float wheelDelta)
{
    wheelRemainder += wheelDelta;
    const auto wholeLines = std::trunc(wheelRemainder);
    wheelRemainder -= wholeLines;
    const auto lines = static_cast<int>(wholeLines);
    return lines;
}

// Full-screen apps get wheel motion as arrow keys, the classic fallback when no
// mouse mode is active.
void TerminalView::sendWheelAsArrowKeys(int lines)
{
    const auto arrows = encodeWheelAsArrowKeys(lines, screen.modes.appCursorKeys);

    for (auto i = 0; i < arrows.count; ++i)
        send(arrows.key);
}

float TerminalView::getPointsPerWheelLine() const
{
    if (!atlas)
        return 0.f;

    return atlas->cellHeight();
}

void TerminalView::sendWheelAsMouseReports(const MouseEvent& event, int lines)
{
    const auto reports = encodeWheelAsMouseReports(lines);

    for (auto i = 0; i < reports.count; ++i)
        sendMouseReport(event, reports.button, true, false);
}

bool TerminalView::isWheelForProgram(const MouseEvent& event) const
{
    return isReportingMouseFor(event) || screen.modes.altScreen;
}

void TerminalView::sendWheelLinesToProgram(const MouseEvent& event, int lines)
{
    if (isReportingMouseFor(event))
        sendWheelAsMouseReports(event, lines);
    else
        sendWheelAsArrowKeys(lines);
}

// The scrollback moves by the trackpad's own points, momentum phase included,
// so it tracks the fingers; a program still gets whole lines.
void TerminalView::mouseWheel(const MouseEvent& event)
{
    const auto lock = lockTerminal();
    stopScrollMomentum();
    const auto wheelLines =
        convertWheelDeltaToLines(event.delta.y, event.preciseScrolling, getPointsPerWheelLine());

    if (!isWheelForProgram(event))
    {
        scrollByRows(wheelLines);
        return;
    }

    const auto lines = takeWholeWheelLines(wheelLines);

    if (lines != 0)
        sendWheelLinesToProgram(event, lines);
}
} // namespace term
