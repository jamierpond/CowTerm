#include <TerminalView/ScrollIndicator.h>
#include <TerminalView/ScrollMomentum.h>
#include <TerminalView/ScrollPosition.h>

#include <NanoTest/NanoTest.h>

#include <cmath>

// Where the reader is in the scrollback, the coast after a fling, the
// finger's speed at lift-off and the scroll indicator, with no view behind
// them.

using namespace nano;
using namespace term;

namespace
{
constexpr auto scrollbackSize = 100L;
constexpr auto gridRows = 25L;
constexpr auto tolerance = 1.0e-3;

bool isNear(double actual, double expected, double allowed = tolerance)
{
    return std::abs(actual - expected) < allowed;
}

// A view exactly as tall as its grid, as on the Mac.
ScrollRange makeRangeForScrollback(long size)
{
    return calculateScrollRange({size, gridRows, gridRows, true});
}

const auto fullRange = makeRangeForScrollback(scrollbackSize);

ScrollbackTotals makeTotals(long linesAdded, long linesDropped)
{
    const auto added = static_cast<std::uint64_t>(linesAdded);
    const auto dropped = static_cast<std::uint64_t>(linesDropped);
    return {added, dropped};
}

std::uint64_t countLines(long lines)
{
    const auto count = static_cast<std::uint64_t>(lines);
    return count;
}

ScrollPosition scrolledBackBy(double rows)
{
    auto position = ScrollPosition {};
    position.scrollByRows(rows, fullRange);
    return position;
}
} // namespace

auto tRangeOfAMacView = test("Scroll/aViewAsTallAsItsGridScrollsFromLineZeroToTheGridTop") = []
{
    check(fullRange.oldestTopRow == 0);
    check(fullRange.liveBottomTopRow == scrollbackSize);
};

auto tRangeOfATallView = test("Scroll/aViewTallerThanItsGridShowsScrollbackAboveTheGrid") = []
{
    constexpr auto keyboardDownRows = 40L;
    const auto range = calculateScrollRange({scrollbackSize, gridRows, keyboardDownRows, true});

    check(range.liveBottomTopRow == scrollbackSize + gridRows - keyboardDownRows,
          "at the live bottom the grid's last row is the view's last row");
    check(range.oldestTopRow == 0);
};

auto tRangeOfShortContent = test("Scroll/shortContentSitsAtTheBottomAndCannotScroll") = []
{
    constexpr auto fewLines = 5L;
    constexpr auto keyboardDownRows = 40L;
    const auto range = calculateScrollRange({fewLines, gridRows, keyboardDownRows, true});

    check(range.liveBottomTopRow == fewLines + gridRows - keyboardDownRows);
    check(range.oldestTopRow == range.liveBottomTopRow, "blank rows above, nothing to scroll");
};

auto tRangeOfAltScreen = test("Scroll/theAlternateScreenNeverScrollsIntoHistory") = []
{
    constexpr auto keyboardDownRows = 40L;
    const auto range = calculateScrollRange({scrollbackSize, gridRows, keyboardDownRows, false});

    check(range.oldestTopRow == range.liveBottomTopRow);
    check(range.liveBottomTopRow == scrollbackSize + gridRows - keyboardDownRows);
};

auto tTallViewScrolls = test("Scroll/aTallViewScrollsBackToLineZeroAtItsTop") = []
{
    constexpr auto keyboardDownRows = 40L;
    constexpr auto pastTheOldest = 1000.0;
    const auto range = calculateScrollRange({scrollbackSize, gridRows, keyboardDownRows, true});
    auto position = ScrollPosition {};

    check(position.findTopAbsoluteRow(range) == range.liveBottomTopRow);
    position.scrollByRows(pastTheOldest, range);
    check(position.findTopAbsoluteRow(range) == 0);
};

auto tKeyboardShown = test("Scroll/aShorterViewKeepsTheTopRowWhileScrolledBack") = []
{
    constexpr auto keyboardDownRows = 40L;
    constexpr auto rowsBack = 30.0;
    const auto tallRange = calculateScrollRange({scrollbackSize, gridRows, keyboardDownRows, true});
    auto position = ScrollPosition {};
    position.scrollByRows(rowsBack, tallRange);
    const auto topBefore = position.findTopAbsoluteRow(tallRange);

    check(position.findTopAbsoluteRow(fullRange) == topBefore,
          "the keyboard coming up hides rows at the bottom, not the top");
};

auto tPartRowScroll = test("Scroll/aPartRowScrollKeepsTheTopRowAndItsHiddenFraction") = []
{
    constexpr auto rowsAndAHalf = 2.5;
    constexpr auto wholeRowsBack = 3;
    constexpr auto halfARow = 0.5;
    const auto position = scrolledBackBy(rowsAndAHalf);

    check(!position.isFollowingLiveBottom());
    check(position.findTopAbsoluteRow(fullRange) == scrollbackSize - wholeRowsBack);
    check(isNear(position.getHiddenFractionOfTopRow(), halfARow));
    check(position.calculateLinesFromBottom(fullRange) == wholeRowsBack);
};

auto tLinesFromBottom = test("Scroll/linesFromBottomRoundTripToTheTopRow") = []
{
    constexpr auto linesFromBottom = 40;
    auto position = ScrollPosition {};
    position.showLinesFromBottom(linesFromBottom, fullRange);

    check(position.findTopAbsoluteRow(fullRange) == scrollbackSize - linesFromBottom);
    check(position.calculateLinesFromBottom(fullRange) == linesFromBottom);
    check(position.getHiddenFractionOfTopRow() == 0.0);

    position.showLinesFromBottom(0, fullRange);
    check(position.isFollowingLiveBottom());
};

auto tFollowsBottom = test("Scroll/atTheLiveBottomTheViewFollowsNewOutput") = []
{
    constexpr auto linesArrived = 7L;
    const auto grownRange = makeRangeForScrollback(scrollbackSize + linesArrived);
    auto position = ScrollPosition {};
    position.applyScrollbackTotals(makeTotals(linesArrived, 0), grownRange);

    check(position.isFollowingLiveBottom());
    check(position.findTopAbsoluteRow(grownRange) == scrollbackSize + linesArrived);
    check(position.countLinesArrivedWhileScrolledBack() == 0);
};

auto tHoldsStill = test("Scroll/scrolledBackTheViewHoldsStillWhileOutputArrives") = []
{
    constexpr auto rowsBack = 10.0;
    constexpr auto wholeRowsBack = 10;
    constexpr auto linesArrived = 214L;
    auto position = scrolledBackBy(rowsBack);
    const auto topBefore = position.findTopAbsoluteRow(fullRange);
    const auto grownRange = makeRangeForScrollback(scrollbackSize + linesArrived);

    position.applyScrollbackTotals(makeTotals(linesArrived, 0), grownRange);

    check(position.findTopAbsoluteRow(grownRange) == topBefore);
    check(position.countLinesArrivedWhileScrolledBack() == countLines(linesArrived));
    check(position.calculateLinesFromBottom(grownRange) == wholeRowsBack + linesArrived);
};

auto tRingDrops = test("Scroll/linesDroppedOffAFullRingMoveTheAnchoredRowWithTheText") = []
{
    constexpr auto rowsBack = 60.0;
    constexpr auto linesThroughTheRing = 5L;
    auto position = scrolledBackBy(rowsBack);
    const auto topBefore = position.findTopAbsoluteRow(fullRange);

    position.applyScrollbackTotals(makeTotals(linesThroughTheRing, linesThroughTheRing), fullRange);

    check(position.findTopAbsoluteRow(fullRange) == topBefore - linesThroughTheRing);
    check(position.countLinesArrivedWhileScrolledBack() == countLines(linesThroughTheRing));
};

auto tRingDropsPastTop = test("Scroll/aRowDroppedOffTheRingClampsTheViewToTheOldestLine") = []
{
    constexpr auto rowsBackToNearTheTop = 98.5;
    constexpr auto linesThroughTheRing = 10L;
    auto position = scrolledBackBy(rowsBackToNearTheTop);

    position.applyScrollbackTotals(makeTotals(linesThroughTheRing, linesThroughTheRing), fullRange);

    check(position.findTopAbsoluteRow(fullRange) == 0);
    check(position.getHiddenFractionOfTopRow() == 0.0);
    check(!position.isFollowingLiveBottom());
};

auto tClearedScrollback = test("Scroll/clearingTheScrollbackReturnsToTheLiveBottom") = []
{
    constexpr auto rowsBack = 30.0;
    auto position = scrolledBackBy(rowsBack);

    position.applyScrollbackTotals(makeTotals(0, scrollbackSize), makeRangeForScrollback(0));

    check(position.isFollowingLiveBottom());
    check(position.countLinesArrivedWhileScrolledBack() == 0);
};

auto tReturningResetsCount = test("Scroll/reachingTheBottomAgainForgetsTheNewLineCount") = []
{
    constexpr auto rowsBack = 3.0;
    constexpr auto linesArrived = 4L;
    const auto grownRange = makeRangeForScrollback(scrollbackSize + linesArrived);
    auto position = scrolledBackBy(rowsBack);
    position.applyScrollbackTotals(makeTotals(linesArrived, 0), grownRange);
    check(position.countLinesArrivedWhileScrolledBack() == countLines(linesArrived));

    constexpr auto pastTheBottom = -50.0;
    position.scrollByRows(pastTheBottom, grownRange);

    check(position.isFollowingLiveBottom());
    check(position.countLinesArrivedWhileScrolledBack() == 0);
};

auto tStateChanges = test("Scroll/leavingTheBottomNewLinesAndReturningEachMarkTheStateChanged") = []
{
    constexpr auto rowsBack = 3.0;
    constexpr auto rowsFurtherBack = 1.0;
    constexpr auto linesArrived = 2L;
    auto position = ScrollPosition {};
    check(!position.takeWhetherScrolledBackStateChanged());

    position.scrollByRows(rowsBack, fullRange);
    check(position.takeWhetherScrolledBackStateChanged(), "left the live bottom");
    check(!position.takeWhetherScrolledBackStateChanged(), "taking it clears it");

    position.scrollByRows(rowsFurtherBack, fullRange);
    check(!position.takeWhetherScrolledBackStateChanged(), "moving while back changes nothing");

    position.applyScrollbackTotals(makeTotals(linesArrived, 0),
                                   makeRangeForScrollback(scrollbackSize + linesArrived));
    check(position.takeWhetherScrolledBackStateChanged(), "lines arrived");

    position.followLiveBottom();
    check(position.takeWhetherScrolledBackStateChanged(), "returned to the live bottom");
};

auto tStopsAtEdges = test("Scroll/scrollingStopsHardAtTheOldestAndNewestLines") = []
{
    constexpr auto pastTheOldest = 500.0;
    constexpr auto oneRowTowardNewer = -1.0;
    auto position = ScrollPosition {};

    check(position.scrollByRows(pastTheOldest, fullRange) == ScrollEdgeContact::StoppedAtEdge);
    check(position.findTopAbsoluteRow(fullRange) == 0);
    check(position.getHiddenFractionOfTopRow() == 0.0);

    check(position.scrollByRows(oneRowTowardNewer, fullRange) == ScrollEdgeContact::None);
    check(position.findTopAbsoluteRow(fullRange) == 1);

    position.followLiveBottom();
    const auto contactAtBottom = position.scrollByRows(oneRowTowardNewer, fullRange);
    check(contactAtBottom == ScrollEdgeContact::StoppedAtEdge);
    check(position.isFollowingLiveBottom());
};

auto tFloatNoise = test("Scroll/pixelsThatMakeWholeRowsLeaveNoSliverOfARow") = []
{
    constexpr auto rowHeight = 16.9;
    constexpr auto rows = 3;
    const auto pixels = rowHeight * rows;
    const auto position = scrolledBackBy(pixels / rowHeight);

    check(position.getHiddenFractionOfTopRow() == 0.0);
    check(position.calculateLinesFromBottom(fullRange) == rows);
};

auto tThumbSize = test("ScrollIndicator/theThumbIsTheViewportsShareOfTheTrack") = []
{
    constexpr auto trackHeight = 800.0;
    constexpr auto scrollableRows = 75.0;
    constexpr auto viewportRows = 25.0;
    constexpr auto quarterOfTheTrack = 200.0;
    const auto atOldest =
        calculateScrollIndicatorThumb({0.0, trackHeight, scrollableRows, viewportRows, 0.0});
    const auto atBottom = calculateScrollIndicatorThumb(
        {0.0, trackHeight, scrollableRows, viewportRows, scrollableRows});

    check(isNear(atOldest.height, quarterOfTheTrack));
    check(isNear(atOldest.top, 0.0));
    check(isNear(atBottom.top + atBottom.height, trackHeight),
          "at the bottom it touches the track end");
};

auto tThumbMinimum = test("ScrollIndicator/aLongScrollbackKeepsAFingerSizedThumb") = []
{
    constexpr auto trackTop = 3.0;
    constexpr auto trackHeight = 600.0;
    constexpr auto scrollableRows = 10000.0;
    constexpr auto viewportRows = 25.0;
    constexpr auto halfway = 5000.0;
    constexpr auto shortestThumbHeight = 36.0;
    const auto thumb = calculateScrollIndicatorThumb(
        {trackTop, trackHeight, scrollableRows, viewportRows, halfway});

    check(isNear(thumb.height, shortestThumbHeight));
    check(isNear(thumb.top, trackTop + (trackHeight - shortestThumbHeight) / 2.0));
};

auto tIndicatorFades = test("ScrollIndicator/theBarHoldsAfterScrollingThenFadesOut") = []
{
    constexpr auto duringTheHold = FractionalSeconds {0.4};
    constexpr auto intoTheFade = FractionalSeconds {0.25};
    constexpr auto pastTheFade = FractionalSeconds {1.0};
    auto fade = ScrollIndicatorFade {};
    check(!fade.isVisible(), "hidden until the first scroll");

    fade.showForScroll();
    fade.advanceBy(duringTheHold);
    check(fade.calculateOpacity() == 1.0);

    fade.advanceBy(intoTheFade);
    check(fade.calculateOpacity() > 0.0 && fade.calculateOpacity() < 1.0);

    fade.advanceBy(pastTheFade);
    check(!fade.isVisible());

    fade.showForScroll();
    check(fade.calculateOpacity() == 1.0, "scrolling again shows it at once");
};

auto tFlingDistance = test("Momentum/aFlingCoversTheIntegralOfItsDecayingSpeed") = []
{
    constexpr auto velocity = 1000.0;
    constexpr auto oneSecond = FractionalSeconds {1.0};
    constexpr auto millisecondsPerSecond = 1000.0;
    const auto expected = velocity / millisecondsPerSecond
                          * (std::pow(normalScrollDecelerationRatePerMillisecond,
                                      oneSecond.count() * millisecondsPerSecond)
                             - 1.0)
                          / std::log(normalScrollDecelerationRatePerMillisecond);

    check(isNear(calculateFlingDistanceOverTime(
                     velocity, oneSecond, normalScrollDecelerationRatePerMillisecond),
                 expected));

    constexpr auto forever = FractionalSeconds {1000.0};
    const auto totalCoast = -velocity / millisecondsPerSecond
                            / std::log(normalScrollDecelerationRatePerMillisecond);
    check(isNear(calculateFlingDistanceOverTime(
                     velocity, forever, normalScrollDecelerationRatePerMillisecond),
                 totalCoast));
};

auto tFrameRateIndependent = test("Momentum/theCoastIsTheSameAtAnyFrameRate") = []
{
    constexpr auto velocity = 1500.0;
    constexpr auto framesPerSecondSlow = 60;
    constexpr auto framesPerSecondFast = 120;
    constexpr auto allowedPixels = 0.05;

    const auto coastForOneSecondAt = [&](int framesPerSecond)
    {
        auto momentum = ScrollMomentum {};
        momentum.startFling(velocity);
        auto travelled = 0.0;

        for (auto frame = 0; frame < framesPerSecond; ++frame)
            travelled += momentum.advanceBy(FractionalSeconds {1.0 / framesPerSecond});

        return travelled;
    };

    check(isNear(coastForOneSecondAt(framesPerSecondSlow),
                 coastForOneSecondAt(framesPerSecondFast),
                 allowedPixels));
};

auto tSpeedDecays = test("Momentum/theSpeedDecaysByTheRateEachMillisecond") = []
{
    constexpr auto velocity = 2000.0;
    constexpr auto tenthOfASecond = FractionalSeconds {0.1};
    constexpr auto millisecondsInStep = 100.0;
    constexpr auto allowedSpeed = 0.01;
    auto momentum = ScrollMomentum {};
    momentum.startFling(velocity);
    momentum.advanceBy(tenthOfASecond);

    const auto expected =
        velocity * std::pow(normalScrollDecelerationRatePerMillisecond, millisecondsInStep);
    check(isNear(momentum.getVelocity(), expected, allowedSpeed));
};

auto tStopsWhenSlow = test("Momentum/aFlingStopsBelowTheMinimumSpeed") = []
{
    constexpr auto velocity = 800.0;
    constexpr auto frameSeconds = FractionalSeconds {1.0 / 60.0};
    constexpr auto framesInTenSeconds = 600;
    auto momentum = ScrollMomentum {};
    momentum.startFling(velocity);

    for (auto frame = 0; frame < framesInTenSeconds && momentum.isMoving(); ++frame)
        momentum.advanceBy(frameSeconds);

    check(!momentum.isMoving());
    check(momentum.advanceBy(frameSeconds) == 0.0);

    momentum.startFling(minimumMomentumSpeed / 2.0);
    check(!momentum.isMoving(), "a lift slower than the minimum never starts a coast");
};

auto tTouchVelocity = test("TouchVelocity/theLastSamplesGiveTheSpeedAtLift") = []
{
    constexpr auto sampleInterval = FractionalSeconds {0.01};
    constexpr auto pixelsPerSample = 12.0;
    constexpr auto slowPixelsPerSample = 1.0;
    constexpr auto samples = 30;
    constexpr auto expectedVelocity = 1200.0;
    constexpr auto allowedSpeed = 0.5;
    auto tracker = TouchVelocityTracker {};
    auto position = 0.0;
    auto time = FractionalSeconds {};

    // A slow start the window forgets, then a fast finish.
    for (auto sample = 0; sample < samples; ++sample, time += sampleInterval)
    {
        tracker.addSample({time, position});
        position += sample < samples / 2 ? slowPixelsPerSample : pixelsPerSample;
    }

    const auto lastSampleTime = time - sampleInterval;
    check(std::abs(tracker.calculateVelocityAtLift(lastSampleTime) - expectedVelocity)
          < allowedSpeed);
};

auto tTouchRest = test("TouchVelocity/aFingerThatRestsBeforeLiftingDoesNotFling") = []
{
    constexpr auto sampleInterval = FractionalSeconds {0.01};
    constexpr auto pixelsPerSample = 20.0;
    constexpr auto restBeforeLift = FractionalSeconds {0.2};
    auto tracker = TouchVelocityTracker {};
    tracker.addSample({FractionalSeconds {}, 0.0});
    tracker.addSample({sampleInterval, pixelsPerSample});

    check(tracker.calculateVelocityAtLift(sampleInterval + restBeforeLift) == 0.0);

    tracker.reset();
    tracker.addSample({FractionalSeconds {}, 0.0});
    check(tracker.calculateVelocityAtLift(FractionalSeconds {}) == 0.0, "one sample has no speed");
};
