#include "TerminalView.h"

#include <cmath>

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
} // namespace

void TerminalView::syncScrollPositionWithScrollback()
{
    const auto totals = ScrollbackTotals {screen.countLinesEverAddedToScrollback(),
                                          screen.countLinesEverDroppedFromScrollback()};
    scrollPosition.applyScrollbackTotals(totals, screen.scrollbackSize());
}

int TerminalView::calculateLinesFromBottom() const
{
    return scrollPosition.calculateLinesFromBottom(screen.scrollbackSize());
}

// Rounded to whole device pixels, so glyphs land on the pixel grid the atlas
// rasterized them for.
float TerminalView::calculatePixelOffsetIntoTopRow() const
{
    const auto offsetInPoints =
        (float) scrollPosition.getHiddenFractionOfTopRow() * atlas->cellHeight();
    const auto devicePixelsPerPoint = atlas->scale();
    return std::round(offsetInPoints * devicePixelsPerPoint) / devicePixelsPerPoint;
}

// A part-scrolled top row lets one more row show at the bottom.
int TerminalView::countRowsToDraw() const
{
    if (calculatePixelOffsetIntoTopRow() > 0.f)
        return screen.rows() + 1;

    return screen.rows();
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
    repaintIfShown();
}

ScrollEdgeContact TerminalView::scrollByRows(double rows)
{
    syncScrollPositionWithScrollback();
    const auto contact = scrollPosition.scrollByRows(rows, screen.scrollbackSize());
    repaintIfShown();
    return contact;
}

ScrollEdgeContact TerminalView::scrollByPixels(float pixels)
{
    return scrollByRows(convertWheelDeltaToLines(pixels, true, atlas->cellHeight()));
}

void TerminalView::scrollBy(int lines)
{
    scrollByRows(lines);
}

void TerminalView::stopScrollMomentum()
{
    scrollMomentum.stop();

    if (isContinuous())
        setContinuous(false);
}

void TerminalView::update(Threads::FrameTime frameTime)
{
    const auto lock = lockTerminal();
    const auto pixels = scrollMomentum.advanceBy(FractionalSeconds {frameTime.delta});
    const auto contact = scrollByPixels(pixels);

    if (contact == ScrollEdgeContact::StoppedAtEdge || !scrollMomentum.isMoving())
        stopScrollMomentum();
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

void TerminalView::endTouchScrollWithVelocity(float velocityPixelsPerSecond)
{
    const auto lock = lockTerminal();
    auto event = MouseEvent {};
    event.pos = lastTouchPosition;
    event.fromTouch = true;
    touchVelocity.reset();

    if (isWheelForProgram(event))
        return;

    scrollMomentum.startFling(velocityPixelsPerSecond);

    if (scrollMomentum.isMoving())
        setContinuous(true);
}

// ---- wheel -----------------------------------------------------------------

int TerminalView::takeWholeWheelLines(float wheelDelta)
{
    wheelRemainder += wheelDelta;
    const auto lines = (int) wheelRemainder;
    wheelRemainder -= (float) lines;
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
