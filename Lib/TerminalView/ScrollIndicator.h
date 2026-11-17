#pragma once

#include "ScrollMomentum.h"

namespace term
{
// Where the view is within everything it can scroll through, in rows.
struct ScrollIndicatorLayout
{
    double trackTop = 0.0;
    double trackHeight = 0.0;
    double scrollableRows = 0.0;
    double viewportRows = 0.0;

    // 0 at the oldest line, scrollableRows at the live bottom.
    double rowsScrolledFromOldest = 0.0;
};

struct ScrollIndicatorThumb
{
    double top = 0.0;
    double height = 0.0;
};

// The thumb's length is the viewport's share of the content, never shorter
// than a finger-sized minimum; its top travels the rest of the track in
// proportion to the position.
ScrollIndicatorThumb calculateScrollIndicatorThumb(const ScrollIndicatorLayout& layout);

// The thin bar on the right edge shows while the view scrolls, then holds and
// fades out, as an iOS list's does.
class ScrollIndicatorFade
{
public:
    void showForScroll();
    void advanceBy(FractionalSeconds elapsed);
    bool isVisible() const { return calculateOpacity() > 0.0; }

    // 1 while scrolling and during the hold, falling to 0 over the fade.
    double calculateOpacity() const;

private:
    FractionalSeconds timeSinceLastScroll = FractionalSeconds::max();
};
} // namespace term
