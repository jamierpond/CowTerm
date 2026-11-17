#include "PaneScreenLayout.h"

#include "Multiplexer/RemoteLocation.h"
#include "Phone/StoredScreen.h"

#include <algorithm>
#include <cmath>

namespace cowterm::mobile
{
namespace
{
constexpr auto pillHeight = 32.0f;
constexpr auto pillGapAbovePaneBottom = 10.0f;

constexpr auto barSidePadding = 8.0f;
constexpr auto barButtonWidth = 52.0f;
constexpr auto widestPaneDotSpacing = 28.0f;

constexpr auto tapSlop = 10.0f;
constexpr auto swipeMinimumDistance = 40.0f;

// A swipe has to run at least this many times further along its axis than
// across it, so a sloppy diagonal is neither.
constexpr auto swipeAxisDominance = 2.0f;


float calculateDistance(Point from, Point to)
{
    return std::hypot(to.x - from.x, to.y - from.y);
}

bool isMostlyAlong(float along, float across)
{
    return std::abs(along) >= swipeAxisDominance * std::abs(across);
}

// The dots sit centred in what the chip and the buttons leave, closer
// together when there are too many for the room.
std::vector<Rect> calculatePaneDots(Rect room, int paneCount)
{
    if (paneCount <= 1 || room.w <= 0.0f)
        return {};

    const auto spacing = std::min(widestPaneDotSpacing, room.w / (float) paneCount);
    const auto rowWidth = spacing * (float) paneCount;
    auto x = room.x + (room.w - rowWidth) / 2.0f;
    auto dots = std::vector<Rect> {};

    for (auto index = 0; index < paneCount; ++index)
    {
        dots.push_back({x, room.y, spacing, room.h});
        x += spacing;
    }

    return dots;
}
} // namespace

PaneScreenRects calculatePaneScreenRects(Rect bounds, Insets safeArea)
{
    auto inside = bounds.inset(safeArea);
    auto rects = PaneScreenRects {};
    rects.topLine = inside.removeFromTop(topLineHeight);
    rects.bottomBar = inside.removeFromBottom(bottomBarHeight);
    rects.pane = inside;
    return rects;
}

TypingRects calculateTypingRects(Rect bounds, Insets insetsAroundParts, bool showsComposer)
{
    auto inside = bounds.inset(insetsAroundParts);
    auto rects = TypingRects {};
    rects.topLine = inside.removeFromTop(topLineHeight);
    rects.keyRow = inside.removeFromBottom(keyRowHeight);

    if (showsComposer)
        rects.composer = inside.removeFromBottom(composerHeight);

    rects.pane = inside;
    return rects;
}

Rect calculateNewLinesPillBounds(Rect pane, float pillWidth)
{
    const auto top = pane.bottom() - pillGapAbovePaneBottom - pillHeight;
    return {pane.center().x - pillWidth / 2.0f, top, pillWidth, pillHeight};
}

BottomBarLayout calculateBottomBarLayout(Rect bar,
                                         std::optional<float> previousSessionWidth,
                                         int paneCount)
{
    auto room = bar.inset(barSidePadding, 0.0f);
    auto layout = BottomBarLayout {};

    if (previousSessionWidth)
        layout.previousSession = room.removeFromLeft(*previousSessionWidth);

    layout.overview = room.removeFromRight(barButtonWidth);
    layout.diff = room.removeFromRight(barButtonWidth);
    layout.keyboard = room.removeFromRight(barButtonWidth);
    layout.paneDots = calculatePaneDots(room, paneCount);
    return layout;
}

BottomBarHit findBottomBarControlAt(const BottomBarLayout& layout, Point position)
{
    if (layout.previousSession && layout.previousSession->contains(position))
        return {BottomBarControl::PreviousSession};

    for (auto index = 0; index < (int) layout.paneDots.size(); ++index)
    {
        if (layout.paneDots[(std::size_t) index].contains(position))
            return {BottomBarControl::PaneDot, index};
    }

    if (layout.keyboard.contains(position))
        return {BottomBarControl::Keyboard};

    if (layout.diff.contains(position))
        return {BottomBarControl::Diff};

    if (layout.overview.contains(position))
        return {BottomBarControl::Overview};

    return {};
}

BottomBarGesture classifyBottomBarRelease(Point start, Point end)
{
    if (!hasMovedBeyondTapSlop(start, end))
        return BottomBarGesture::Tap;

    const auto across = end.x - start.x;
    const auto up = start.y - end.y;

    if (std::abs(across) >= swipeMinimumDistance && isMostlyAlong(across, up))
    {
        if (across < 0.0f)
            return BottomBarGesture::SwipeToNextPane;

        return BottomBarGesture::SwipeToPreviousPane;
    }

    if (up >= swipeMinimumDistance && isMostlyAlong(up, across))
        return BottomBarGesture::SwipeUp;

    return BottomBarGesture::Unclear;
}

bool hasMovedBeyondTapSlop(Point start, Point current)
{
    return calculateDistance(start, current) > tapSlop;
}

bool isStartingAtTheLeftEdge(Point start)
{
    constexpr auto leftEdgeWidth = 24.0f;
    return start.x <= leftEdgeWidth;
}

bool isSwipeFromTheLeftEdge(Point start, Point end)
{
    const auto across = end.x - start.x;
    const auto down = end.y - start.y;
    return isStartingAtTheLeftEdge(start) && across >= swipeMinimumDistance && isMostlyAlong(across, down);
}

Point moveOntoTheBarsRow(Point position)
{
    constexpr auto middleOfTheRow = bottomBarHeight / 2.0f;
    return {position.x, std::min(position.y, middleOfTheRow)};
}

int choosePaneAfterSwipe(int activePane, int paneCount, BottomBarGesture gesture)
{
    if (gesture == BottomBarGesture::SwipeToNextPane)
        return std::min(activePane + 1, paneCount - 1);

    if (gesture == BottomBarGesture::SwipeToPreviousPane)
        return std::max(activePane - 1, 0);

    return activePane;
}

std::string describeLinkStatus(LinkState state, std::int64_t pictureAgeMilliseconds)
{
    if (state == LinkState::Live)
        return "\xe2\x97\x8f live";

    const auto age = " \xc2\xb7 " + term::phone::describeStoredScreenAge(pictureAgeMilliseconds);

    if (state == LinkState::Connecting)
        return "connecting" + age;

    if (state == LinkState::Unreachable)
        return "unreachable" + age;

    return "stalled" + age;
}

std::string describePaneTitle(const std::string& host, const std::string& sessionName)
{
    const auto label = term::remote::getHostLabel(host);

    if (sessionName.empty())
        return label;

    return label + "/" + sessionName;
}

std::string formatNewLinesPill(std::uint64_t lineCount)
{
    return "\xe2\x86\x93 " + std::to_string(lineCount) + " new";
}
} // namespace cowterm::mobile
