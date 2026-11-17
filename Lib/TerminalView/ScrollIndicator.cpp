#include "ScrollIndicator.h"

#include <algorithm>

namespace term
{
namespace
{
constexpr auto shortestThumbHeight = 36.0;
constexpr auto holdAfterScroll = FractionalSeconds {0.5};
constexpr auto fadeOutTime = FractionalSeconds {0.3};
} // namespace

ScrollIndicatorThumb calculateScrollIndicatorThumb(const ScrollIndicatorLayout& layout)
{
    const auto contentRows = layout.scrollableRows + layout.viewportRows;
    const auto viewportShare = layout.viewportRows / contentRows;
    const auto shortestHeight = std::min(shortestThumbHeight, layout.trackHeight);
    const auto shareOfTrack = layout.trackHeight * viewportShare;
    const auto height = std::clamp(shareOfTrack, shortestHeight, layout.trackHeight);

    if (layout.scrollableRows <= 0.0)
        return {layout.trackTop, height};

    const auto travel = layout.trackHeight - height;
    const auto progress =
        std::clamp(layout.rowsScrolledFromOldest / layout.scrollableRows, 0.0, 1.0);
    return {layout.trackTop + travel * progress, height};
}

void ScrollIndicatorFade::showForScroll()
{
    timeSinceLastScroll = FractionalSeconds::zero();
}

void ScrollIndicatorFade::advanceBy(FractionalSeconds elapsed)
{
    if (!isVisible())
        return;

    timeSinceLastScroll += elapsed;
}

double ScrollIndicatorFade::calculateOpacity() const
{
    if (timeSinceLastScroll <= holdAfterScroll)
        return 1.0;

    const auto fadeElapsed = timeSinceLastScroll - holdAfterScroll;

    if (fadeElapsed >= fadeOutTime)
        return 0.0;

    return 1.0 - fadeElapsed / fadeOutTime;
}
} // namespace term
