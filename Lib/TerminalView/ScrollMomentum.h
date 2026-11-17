#pragma once

#include <chrono>
#include <vector>

namespace term
{
using FractionalSeconds = std::chrono::duration<double>;

// UIScrollView.DecelerationRate.normal: the share of its speed a fling keeps
// after each millisecond.
inline constexpr double normalScrollDecelerationRatePerMillisecond = 0.998;

// Slower than this, in pixels per second, a fling has stopped.
inline constexpr double minimumMomentumSpeed = 20.0;

// The pixels a fling at this speed covers over this time while its speed
// decays by the rate each millisecond: the integral of v * rate^t.
double calculateFlingDistanceOverTime(double velocityPixelsPerSecond,
                                      FractionalSeconds elapsed,
                                      double decelerationRatePerMillisecond);

// The coast after a finger lifts: an exponentially decaying speed, stepped by
// elapsed time so the distance does not depend on the frame rate.
class ScrollMomentum
{
public:
    void startFling(double velocityPixelsPerSecond);
    void stop();
    bool isMoving() const { return velocity != 0.0; }
    double getVelocity() const { return velocity; }

    // The pixels to scroll for this step; the speed decays as it goes.
    double advanceBy(FractionalSeconds elapsed);

private:
    double velocity = 0.0;
};

struct TouchSample
{
    FractionalSeconds time {};
    double position = 0.0;
};

// The finger's speed at lift-off, from its last few positions, for touch
// input that reports positions but no velocity.
class TouchVelocityTracker
{
public:
    void reset();
    void addSample(TouchSample sample);
    double calculateVelocityAtLift(FractionalSeconds liftTime) const;

private:
    std::vector<TouchSample> samples;
};
} // namespace term
