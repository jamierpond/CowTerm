#include "PaneScreenLayout.h"

#include <NanoTest/NanoTest.h>

// The phone's pane screen: where its parts go, the keyboard-up row count, and
// what a finger on the bottom bar did. iOS/PaneScreenLayout.cpp, built here
// on the Mac.

using namespace nano;
using namespace cowterm::mobile;

namespace
{
constexpr auto iPhone17eWidth = 390.0f;
constexpr auto iPhone17eHeight = 844.0f;
constexpr auto iPhone17eSafeAreaTop = 47.0f;
constexpr auto iPhone17eSafeAreaBottom = 34.0f;
constexpr auto chipWidth = 96.0f;
constexpr auto swipeLength = 80.0f;
constexpr auto smallWobble = 4.0f;
constexpr auto dragShorterThanASwipe = 20.0f;
constexpr auto pillWidth = 120.0f;
constexpr auto measuredIPhone17eKeyboardHeight = 301.0f;
constexpr auto manyPanes = 40;
constexpr auto layoutTolerance = 0.01f;

const auto iPhone17eBounds = Rect {0.0f, 0.0f, iPhone17eWidth, iPhone17eHeight};
const auto iPhone17eSafeArea =
    Insets {.top = iPhone17eSafeAreaTop, .bottom = iPhone17eSafeAreaBottom};
const auto bar = Rect {0.0f, 0.0f, iPhone17eWidth, bottomBarHeight};
const auto barMiddle = Point {iPhone17eWidth / 2.0f, bottomBarHeight / 2.0f};
} // namespace

auto tRects = test("PaneScreenLayout/partsStackInsideTheSafeArea") = []
{
    const auto rects = calculatePaneScreenRects(iPhone17eBounds, iPhone17eSafeArea);

    check(rects.topLine.y == iPhone17eSafeAreaTop);
    check(rects.topLine.h == topLineHeight);
    check(rects.pane.y == iPhone17eSafeAreaTop + topLineHeight);
    check(rects.bottomBar.bottom() == iPhone17eHeight - iPhone17eSafeAreaBottom);
    check(rects.pane.bottom() == rects.bottomBar.y);
    check(rects.pane.w == iPhone17eWidth);
};

auto tPill = test("PaneScreenLayout/pillSitsCentredAboveThePaneBottom") = []
{
    const auto pane = calculatePaneScreenRects(iPhone17eBounds, iPhone17eSafeArea).pane;
    const auto pill = calculateNewLinesPillBounds(pane, pillWidth);

    check(pill.center().x == pane.center().x);
    check(pill.bottom() < pane.bottom());
    check(pill.y > pane.y);
};

auto tBarWithEverything = test("PaneScreenLayout/barPlacesChipDotsAndButtons") = []
{
    const auto layout = calculateBottomBarLayout(bar, chipWidth, 3);

    check(layout.previousSession.has_value());
    check(layout.paneDots.size() == 3);
    check(layout.previousSession->right() <= layout.paneDots.front().x);
    check(layout.paneDots.back().right() <= layout.keyboard.x);
    check(layout.keyboard.right() <= layout.diff.x);
    check(layout.diff.right() <= layout.overview.x);
    check(layout.overview.right() <= iPhone17eWidth);
};

auto tBarOnePane = test("PaneScreenLayout/onePaneSessionHasNoDotsAndNoChipWithoutHistory") = []
{
    const auto layout = calculateBottomBarLayout(bar, std::nullopt, 1);

    check(!layout.previousSession.has_value());
    check(layout.paneDots.empty());
};

auto tBarManyPanes = test("PaneScreenLayout/manyDotsShareTheRoomTheyHave") = []
{
    const auto layout = calculateBottomBarLayout(bar, chipWidth, manyPanes);

    check(layout.paneDots.size() == manyPanes);
    check(layout.paneDots.front().x >= layout.previousSession->right());
    check(layout.paneDots.back().right() <= layout.keyboard.x + layoutTolerance);
};

auto tBarHits = test("PaneScreenLayout/findsTheControlUnderAFinger") = []
{
    const auto layout = calculateBottomBarLayout(bar, chipWidth, 3);

    check(findBottomBarControlAt(layout, layout.overview.center()).control
          == BottomBarControl::Overview);
    check(findBottomBarControlAt(layout, layout.keyboard.center()).control
          == BottomBarControl::Keyboard);
    check(findBottomBarControlAt(layout, layout.previousSession->center()).control
          == BottomBarControl::PreviousSession);

    const auto secondDot = findBottomBarControlAt(layout, layout.paneDots[1].center());
    check(secondDot.control == BottomBarControl::PaneDot);
    check(secondDot.paneIndex == 1);
};

auto tGestures = test("PaneScreenLayout/classifiesSwipesAndTapsOnTheBar") = []
{
    const auto left = Point {barMiddle.x - swipeLength, barMiddle.y};
    const auto right = Point {barMiddle.x + swipeLength, barMiddle.y};
    const auto up = Point {barMiddle.x, barMiddle.y - swipeLength};
    const auto wobble = Point {barMiddle.x + smallWobble, barMiddle.y + smallWobble};
    const auto diagonal = Point {barMiddle.x + swipeLength, barMiddle.y - swipeLength};

    check(classifyBottomBarRelease(barMiddle, wobble) == BottomBarGesture::Tap);
    check(classifyBottomBarRelease(barMiddle, left) == BottomBarGesture::SwipeToNextPane);
    check(classifyBottomBarRelease(barMiddle, right)
          == BottomBarGesture::SwipeToPreviousPane);
    check(classifyBottomBarRelease(barMiddle, up) == BottomBarGesture::SwipeUp);
    check(classifyBottomBarRelease(barMiddle, diagonal) == BottomBarGesture::Unclear);
};

auto tShortDrag = test("PaneScreenLayout/aShortDragIsNeitherTapNorSwipe") = []
{
    const auto shortDragEnd = Point {barMiddle.x - dragShorterThanASwipe, barMiddle.y};

    check(hasMovedBeyondTapSlop(barMiddle, shortDragEnd));
    check(classifyBottomBarRelease(barMiddle, shortDragEnd) == BottomBarGesture::Unclear);
};

auto tPaneAfterSwipe = test("PaneScreenLayout/swipesStopAtTheSessionsEnds") = []
{
    check(choosePaneAfterSwipe(0, 3, BottomBarGesture::SwipeToNextPane) == 1);
    check(choosePaneAfterSwipe(2, 3, BottomBarGesture::SwipeToNextPane) == 2);
    check(choosePaneAfterSwipe(0, 3, BottomBarGesture::SwipeToPreviousPane) == 0);
    check(choosePaneAfterSwipe(1, 3, BottomBarGesture::SwipeUp) == 1);
};

auto tPillText = test("PaneScreenLayout/pillCountsTheNewLines") = []
{
    check(formatNewLinesPill(214) == "\xe2\x86\x93 214 new");
};

auto tLinkStatus = test("PaneScreenLayout/notLivePanesSayHowOldTheirPictureIs") = []
{
    constexpr auto twelveSeconds = std::int64_t {12000};

    check(describeLinkStatus(LinkState::Live, twelveSeconds) == "\xe2\x97\x8f live");
    check(describeLinkStatus(LinkState::Connecting, twelveSeconds)
          == "connecting \xc2\xb7 12 s ago");
    check(describeLinkStatus(LinkState::Stalled, twelveSeconds)
          == "stalled \xc2\xb7 12 s ago");
    check(describeLinkStatus(LinkState::Unreachable, twelveSeconds)
          == "unreachable \xc2\xb7 12 s ago");
};

auto tTitle = test("PaneScreenLayout/titleNamesHostLabelAndSession") = []
{
    check(describePaneTitle("jamiepond@macprorack", "Grazer") == "macprorack/Grazer");
    check(describePaneTitle("jamiepond@macprorack", "") == "macprorack");
    check(describePaneTitle("tamby", "CowTerm") == "tamby/CowTerm");
};

auto tTitleOnce = test("PaneScreenLayout/titleNeverRepeatsTheHostTheSessionNameStartsWith") = []
{
    check(describePaneTitle("macprorack", "macprorack/iac") == "macprorack/iac");
    check(describePaneTitle("jamiepond@macprorack", "macprorack/iac") == "macprorack/iac");
    check(describePaneTitle("macprorack", "macprorack-tools") == "macprorack/macprorack-tools");
};

auto tTypingRects = test("PaneScreenLayout/typingPutsTheKeyRowOnTheKeyboardUnderThePane") = []
{
    const auto keyboardUp =
        Insets {.top = iPhone17eSafeAreaTop, .bottom = measuredIPhone17eKeyboardHeight};
    const auto rects = calculateTypingRects(iPhone17eBounds, keyboardUp);

    check(rects.keyRow.bottom() == iPhone17eHeight - measuredIPhone17eKeyboardHeight);
    check(rects.pane.bottom() == rects.keyRow.y);
};

namespace
{
constexpr auto iPhone12MiniWidth = 375.0f;
constexpr auto iPhone12MiniHeight = 812.0f;
constexpr auto iPhone12MiniSafeAreaTop = 50.0f;
constexpr auto iPhone12MiniSafeAreaBottom = 34.0f;
constexpr auto threeDots = 3;
constexpr auto fingerOverTheHomeIndicator = 20.0f;
constexpr auto backSwipeLength = 120.0f;
constexpr auto edgeStart = 6.0f;
constexpr auto middleOfTheScreen = 180.0f;

// The bar as BottomBar sees it on a 12 mini: its own coordinates, running on
// under the home indicator.
BottomBarLayout layOutIPhone12MiniBar()
{
    const auto screen = Rect {0.0f, 0.0f, iPhone12MiniWidth, iPhone12MiniHeight};
    const auto safeArea = Insets {.top = iPhone12MiniSafeAreaTop, .bottom = iPhone12MiniSafeAreaBottom};
    const auto rects = calculatePaneScreenRects(screen, safeArea);
    const auto barInItsOwnCoordinates = Rect {0.0f, 0.0f, rects.bottomBar.w, bottomBarHeight};
    return calculateBottomBarLayout(barInItsOwnCoordinates, chipWidth, threeDots);
}
} // namespace

auto tIPhone12MiniBarHits = test("PaneScreenLayout/everyBarControlTakesAFingerOnA12Mini") = []
{
    const auto layout = layOutIPhone12MiniBar();

    check(layout.overview.right() <= iPhone12MiniWidth);
    check(findBottomBarControlAt(layout, layout.overview.center()).control == BottomBarControl::Overview);
    check(findBottomBarControlAt(layout, layout.keyboard.center()).control == BottomBarControl::Keyboard);
    check(findBottomBarControlAt(layout, layout.diff.center()).control == BottomBarControl::Diff);
    check(findBottomBarControlAt(layout, layout.previousSession->center()).control
          == BottomBarControl::PreviousSession);

    for (auto index = 0; index < threeDots; ++index)
    {
        const auto dot = findBottomBarControlAt(layout, layout.paneDots[(std::size_t) index].center());
        check(dot.control == BottomBarControl::PaneDot && dot.paneIndex == index);
    }
};

auto tFingerBelowTheRow = test("PaneScreenLayout/aFingerOverTheHomeIndicatorMeansTheControlAbove") = []
{
    const auto layout = layOutIPhone12MiniBar();
    const auto belowOverview = Point {layout.overview.center().x, bottomBarHeight + fingerOverTheHomeIndicator};
    const auto belowKeyboard = Point {layout.keyboard.center().x, bottomBarHeight + fingerOverTheHomeIndicator};

    check(findBottomBarControlAt(layout, moveOntoTheBarsRow(belowOverview)).control == BottomBarControl::Overview);
    check(findBottomBarControlAt(layout, moveOntoTheBarsRow(belowKeyboard)).control == BottomBarControl::Keyboard);
};

auto tBackSwipe = test("PaneScreenLayout/swipeFromTheLeftEdgeGoesBack") = []
{
    const auto start = Point {edgeStart, middleOfTheScreen};

    check(isSwipeFromTheLeftEdge(start, {edgeStart + backSwipeLength, middleOfTheScreen + smallWobble}));
    check(!isSwipeFromTheLeftEdge({middleOfTheScreen, middleOfTheScreen},
                                  {middleOfTheScreen + backSwipeLength, middleOfTheScreen}),
          "only from the edge");
    check(!isSwipeFromTheLeftEdge(start, {edgeStart + smallWobble, middleOfTheScreen + backSwipeLength}),
          "a drag down from the edge is not back");
};
