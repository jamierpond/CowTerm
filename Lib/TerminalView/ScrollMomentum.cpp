#include "ScrollMomentum.h"

#include <algorithm>
#include <cmath>

namespace term
{
namespace
{
using FractionalMilliseconds = std::chrono::duration<double, std::milli>;

// Only the finger's last stretch of travel says how fast it left the glass.
constexpr auto touchVelocitySampleWindow = FractionalSeconds {0.1};

// A finger held still this long before lifting lifts with no fling.
constexpr auto fingerRestBeforeLift = FractionalSeconds {0.05};

// rate^t with t in milliseconds: the share of its speed a fling keeps.
double calculateShareOfSpeedKept(FractionalSeconds elapsed, double decelerationRatePerMillisecond)
{
    const auto elapsedMilliseconds = FractionalMilliseconds {elapsed}.count();
    return std::pow(decelerationRatePerMillisecond, elapsedMilliseconds);
}
} // namespace

double calculateFlingDistanceOverTime(double velocityPixelsPerSecond,
                                      FractionalSeconds elapsed,
                                      double decelerationRatePerMillisecond)
{
    const auto millisecondsPerSecond = FractionalMilliseconds {FractionalSeconds {1.0}}.count();
    const auto velocityPixelsPerMillisecond = velocityPixelsPerSecond / millisecondsPerSecond;
    const auto speedKept = calculateShareOfSpeedKept(elapsed, decelerationRatePerMillisecond);
    return velocityPixelsPerMillisecond * (speedKept - 1.0)
           / std::log(decelerationRatePerMillisecond);
}

void ScrollMomentum::startFling(double velocityPixelsPerSecond)
{
    velocity = velocityPixelsPerSecond;

    if (std::abs(velocity) < minimumMomentumSpeed)
        stop();
}

void ScrollMomentum::stop()
{
    velocity = 0.0;
}

double ScrollMomentum::advanceBy(FractionalSeconds elapsed)
{
    if (!isMoving())
        return 0.0;

    const auto distance = calculateFlingDistanceOverTime(
        velocity, elapsed, normalScrollDecelerationRatePerMillisecond);
    const auto speedKept =
        calculateShareOfSpeedKept(elapsed, normalScrollDecelerationRatePerMillisecond);
    velocity *= speedKept;

    if (std::abs(velocity) < minimumMomentumSpeed)
        stop();

    return distance;
}

void TouchVelocityTracker::reset()
{
    samples.clear();
}

void TouchVelocityTracker::addSample(TouchSample sample)
{
    samples.push_back(sample);

    const auto oldestKept = sample.time - touchVelocitySampleWindow;
    const auto isTooOld = [oldestKept](const TouchSample& kept) { return kept.time < oldestKept; };

    samples.erase(std::remove_if(samples.begin(), samples.end(), isTooOld), samples.end());
}

double TouchVelocityTracker::calculateVelocityAtLift(FractionalSeconds liftTime) const
{
    if (samples.size() < 2)
        return 0.0;

    const auto& first = samples.front();
    const auto& last = samples.back();

    if (liftTime - last.time > fingerRestBeforeLift)
        return 0.0;

    const auto elapsed = last.time - first.time;

    if (elapsed <= FractionalSeconds::zero())
        return 0.0;

    return (last.position - first.position) / elapsed.count();
}
} // namespace term
