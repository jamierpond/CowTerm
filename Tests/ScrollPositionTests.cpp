#include <TerminalView/ScrollMomentum.h>
#include <TerminalView/ScrollPosition.h>

#include <NanoTest/NanoTest.h>

#include <cmath>

// Where the reader is in the scrollback, the coast after a fling and the
// finger's speed at lift-off, with no view behind them.

using namespace nano;
using namespace term;

namespace
{
constexpr auto scrollbackSize = 100;
constexpr auto tolerance = 1.0e-3;

bool isNear(double actual, double expected, double allowed = tolerance)
{
    return std::abs(actual - expected) < allowed;
}

ScrollPosition scrolledBackBy(double rows)
{
    auto position = ScrollPosition {};
    position.scrollByRows(rows, scrollbackSize);
    return position;
}
} // namespace

auto tPartRowScroll = test("Scroll/aPartRowScrollKeepsTheTopRowAndItsHiddenFraction") = []
{
    constexpr auto rowsAndAHalf = 2.5;
    const auto position = scrolledBackBy(rowsAndAHalf);

    check(!position.isFollowingLiveBottom());
    check(position.findTopAbsoluteRow(scrollbackSize) == scrollbackSize - 3);
    check(isNear(position.getHiddenFractionOfTopRow(), 0.5));
    check(position.calculateLinesFromBottom(scrollbackSize) == 3);
};

auto tLinesFromBottom = test("Scroll/linesFromBottomRoundTripToTheTopRow") = []
{
    constexpr auto linesFromBottom = 40;
    auto position = ScrollPosition {};
    position.showLinesFromBottom(linesFromBottom, scrollbackSize);

    check(position.findTopAbsoluteRow(scrollbackSize) == scrollbackSize - linesFromBottom);
    check(position.calculateLinesFromBottom(scrollbackSize) == linesFromBottom);
    check(position.getHiddenFractionOfTopRow() == 0.0);

    position.showLinesFromBottom(0, scrollbackSize);
    check(position.isFollowingLiveBottom());
};

auto tFollowsBottom = test("Scroll/atTheLiveBottomTheViewFollowsNewOutput") = []
{
    constexpr auto linesArrived = 7u;
    auto position = ScrollPosition {};
    position.applyScrollbackTotals({linesArrived, 0}, scrollbackSize + (int) linesArrived);

    check(position.isFollowingLiveBottom());
    check(position.findTopAbsoluteRow(scrollbackSize + (int) linesArrived)
          == scrollbackSize + (long) linesArrived);
    check(position.countLinesArrivedWhileScrolledBack() == 0);
};

auto tHoldsStill = test("Scroll/scrolledBackTheViewHoldsStillWhileOutputArrives") = []
{
    constexpr auto rowsBack = 10.0;
    constexpr auto linesArrived = 214u;
    auto position = scrolledBackBy(rowsBack);
    const auto topBefore = position.findTopAbsoluteRow(scrollbackSize);
    const auto grownScrollback = scrollbackSize + (int) linesArrived;

    position.applyScrollbackTotals({linesArrived, 0}, grownScrollback);

    check(position.findTopAbsoluteRow(grownScrollback) == topBefore);
    check(position.countLinesArrivedWhileScrolledBack() == linesArrived);
    check(position.calculateLinesFromBottom(grownScrollback)
          == (int) rowsBack + (int) linesArrived);
};

auto tRingDrops = test("Scroll/linesDroppedOffAFullRingMoveTheAnchoredRowWithTheText") = []
{
    constexpr auto rowsBack = 60.0;
    constexpr auto linesThroughTheRing = 5u;
    auto position = scrolledBackBy(rowsBack);
    const auto topBefore = position.findTopAbsoluteRow(scrollbackSize);

    position.applyScrollbackTotals({linesThroughTheRing, linesThroughTheRing}, scrollbackSize);

    check(position.findTopAbsoluteRow(scrollbackSize) == topBefore - (long) linesThroughTheRing);
    check(position.countLinesArrivedWhileScrolledBack() == linesThroughTheRing);
};

auto tRingDropsPastTop = test("Scroll/aRowDroppedOffTheRingClampsTheViewToTheOldestLine") = []
{
    constexpr auto rowsBackToNearTheTop = 98.5;
    constexpr auto linesThroughTheRing = 10u;
    auto position = scrolledBackBy(rowsBackToNearTheTop);

    position.applyScrollbackTotals({linesThroughTheRing, linesThroughTheRing}, scrollbackSize);

    check(position.findTopAbsoluteRow(scrollbackSize) == 0);
    check(position.getHiddenFractionOfTopRow() == 0.0);
    check(!position.isFollowingLiveBottom());
};

auto tClearedScrollback = test("Scroll/clearingTheScrollbackReturnsToTheLiveBottom") = []
{
    constexpr auto rowsBack = 30.0;
    auto position = scrolledBackBy(rowsBack);

    position.applyScrollbackTotals({0, scrollbackSize}, 0);

    check(position.isFollowingLiveBottom());
    check(position.countLinesArrivedWhileScrolledBack() == 0);
};

auto tReturningResetsCount = test("Scroll/reachingTheBottomAgainForgetsTheNewLineCount") = []
{
    constexpr auto rowsBack = 3.0;
    constexpr auto linesArrived = 4u;
    auto position = scrolledBackBy(rowsBack);
    position.applyScrollbackTotals({linesArrived, 0}, scrollbackSize + (int) linesArrived);
    check(position.countLinesArrivedWhileScrolledBack() == linesArrived);

    constexpr auto pastTheBottom = -50.0;
    position.scrollByRows(pastTheBottom, scrollbackSize + (int) linesArrived);

    check(position.isFollowingLiveBottom());
    check(position.countLinesArrivedWhileScrolledBack() == 0);
};

auto tStopsAtEdges = test("Scroll/scrollingStopsHardAtTheOldestAndNewestLines") = []
{
    constexpr auto pastTheOldest = 500.0;
    constexpr auto oneRowTowardNewer = -1.0;
    auto position = ScrollPosition {};

    check(position.scrollByRows(pastTheOldest, scrollbackSize) == ScrollEdgeContact::StoppedAtEdge);
    check(position.findTopAbsoluteRow(scrollbackSize) == 0);
    check(position.getHiddenFractionOfTopRow() == 0.0);

    check(position.scrollByRows(oneRowTowardNewer, scrollbackSize) == ScrollEdgeContact::None);
    check(position.findTopAbsoluteRow(scrollbackSize) == 1);

    position.followLiveBottom();
    const auto contactAtBottom = position.scrollByRows(oneRowTowardNewer, scrollbackSize);
    check(contactAtBottom == ScrollEdgeContact::StoppedAtEdge);
    check(position.isFollowingLiveBottom());
};

auto tFloatNoise = test("Scroll/pixelsThatMakeWholeRowsLeaveNoSliverOfARow") = []
{
    constexpr auto rowHeight = 16.9f;
    constexpr auto rows = 3;
    const auto pixels = rowHeight * (float) rows;
    const auto position = scrolledBackBy((double) pixels / (double) rowHeight);

    check(position.getHiddenFractionOfTopRow() == 0.0);
    check(position.calculateLinesFromBottom(scrollbackSize) == rows);
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
    constexpr auto velocity = 1500.f;
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
    constexpr auto velocity = 2000.f;
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
    constexpr auto velocity = 800.f;
    constexpr auto frameSeconds = FractionalSeconds {1.0 / 60.0};
    constexpr auto framesInTenSeconds = 600;
    auto momentum = ScrollMomentum {};
    momentum.startFling(velocity);

    for (auto frame = 0; frame < framesInTenSeconds && momentum.isMoving(); ++frame)
        momentum.advanceBy(frameSeconds);

    check(!momentum.isMoving());
    check(momentum.advanceBy(frameSeconds) == 0.f);

    momentum.startFling(minimumMomentumSpeed / 2.f);
    check(!momentum.isMoving(), "a lift slower than the minimum never starts a coast");
};

auto tTouchVelocity = test("TouchVelocity/theLastSamplesGiveTheSpeedAtLift") = []
{
    constexpr auto sampleInterval = FractionalSeconds {0.01};
    constexpr auto pixelsPerSample = 12.f;
    constexpr auto slowPixelsPerSample = 1.f;
    constexpr auto samples = 30;
    constexpr auto expectedVelocity = 1200.f;
    constexpr auto allowedSpeed = 0.5f;
    auto tracker = TouchVelocityTracker {};
    auto position = 0.f;
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
    constexpr auto pixelsPerSample = 20.f;
    constexpr auto restBeforeLift = FractionalSeconds {0.2};
    auto tracker = TouchVelocityTracker {};
    tracker.addSample({FractionalSeconds {}, 0.f});
    tracker.addSample({sampleInterval, pixelsPerSample});

    check(tracker.calculateVelocityAtLift(sampleInterval + restBeforeLift) == 0.f);

    tracker.reset();
    tracker.addSample({FractionalSeconds {}, 0.f});
    check(tracker.calculateVelocityAtLift(FractionalSeconds {}) == 0.f, "one sample has no speed");
};
