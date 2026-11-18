#pragma once

#include <eacp/Graphics/View/View.h>

namespace cowterm::mobile
{
// A finger put down on the window's root view at `from`, moved to `to` in a
// few steps and lifted: routed from the root as a real touch is, so whatever
// view lies on top at `from` takes it. For tests and launch flags; a headless
// simulator has no finger.
inline void moveFingerOnWindow(eacp::Graphics::View& root,
                               eacp::Graphics::Point from,
                               eacp::Graphics::Point to)
{
    constexpr auto fingerId = 7;
    constexpr auto movementSteps = 4;

    auto touch = eacp::Graphics::TouchEvent {};
    touch.id = fingerId;
    touch.pos = from;
    touch.downPos = from;
    root.dispatchTouchEvent(touch);

    touch.phase = eacp::Graphics::TouchPhase::Moved;
    const auto isDrag = from.x != to.x || from.y != to.y;

    for (auto step = 1; isDrag && step <= movementSteps; ++step)
    {
        const auto fraction = static_cast<float>(step) / static_cast<float>(movementSteps);
        touch.pos = {from.x + (to.x - from.x) * fraction, from.y + (to.y - from.y) * fraction};
        root.dispatchTouchEvent(touch);
    }

    touch.phase = eacp::Graphics::TouchPhase::Ended;
    root.dispatchTouchEvent(touch);
}

inline void tapWithFingerOnWindow(eacp::Graphics::View& root, eacp::Graphics::Point at)
{
    moveFingerOnWindow(root, at, at);
}
} // namespace cowterm::mobile
