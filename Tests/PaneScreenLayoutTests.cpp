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
constexpr auto screenTooShortForARow = 300.0f;
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

auto tKeyboardUpHeight = test("PaneScreenLayout/keyboardUpPaneLeavesRoomForComposerAndKeys") = []
{
    const auto height =
        calculateKeyboardUpPaneHeight({.screenHeight = iPhone17eHeight,
                                       .safeAreaTop = iPhone17eSafeAreaTop,
                                       .softwareKeyboardHeight = measuredIPhone17eKeyboardHeight});

    // 844 - 47 - 28 - 301 - 48 - 44 points.
    check(height == 376.0f);
};

auto tKeyboardUpDefault = test("PaneScreenLayout/keyboardHeightDefaultsBeforeItHasShown") = []
{
    const auto withDefault = calculateKeyboardUpPaneHeight(
        {.screenHeight = iPhone17eHeight, .safeAreaTop = iPhone17eSafeAreaTop});
    const auto withMeasured =
        calculateKeyboardUpPaneHeight({.screenHeight = iPhone17eHeight,
                                       .safeAreaTop = iPhone17eSafeAreaTop,
                                       .softwareKeyboardHeight = defaultSoftwareKeyboardHeight});

    check(withDefault == withMeasured);
};

auto tKeyboardUpFloor = test("PaneScreenLayout/keyboardUpPaneNeverGoesNegative") = []
{
    check(calculateKeyboardUpPaneHeight({.screenHeight = screenTooShortForARow}) == 0.0f);
};

auto tBarWithEverything = test("PaneScreenLayout/barPlacesChipDotsAndButtons") = []
{
    const auto layout = calculateBottomBarLayout(bar, chipWidth, 3);

    check(layout.previousSession.has_value());
    check(layout.paneDots.size() == 3);
    check(layout.previousSession->right() <= layout.paneDots.front().x);
    check(layout.paneDots.back().right() <= layout.keyboard.x);
    check(layout.keyboard.right() <= layout.git.x);
    check(layout.git.right() <= layout.overview.x);
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

auto tTypingRects = test("PaneScreenLayout/typingStacksComposerAndKeyRowOnTheKeyboard") = []
{
    const auto keyboardUp =
        Insets {.top = iPhone17eSafeAreaTop, .bottom = measuredIPhone17eKeyboardHeight};
    const auto rects = calculateTypingRects(iPhone17eBounds, keyboardUp, true);

    check(rects.keyRow.bottom() == iPhone17eHeight - measuredIPhone17eKeyboardHeight);
    check(rects.composer.bottom() == rects.keyRow.y);
    check(rects.pane.bottom() == rects.composer.y);
    check(rects.pane.h
          == calculateKeyboardUpPaneHeight({.screenHeight = iPhone17eHeight,
                                            .safeAreaTop = iPhone17eSafeAreaTop,
                                            .softwareKeyboardHeight = measuredIPhone17eKeyboardHeight}));
};

auto tTypingRectsRaw = test("PaneScreenLayout/rawModeHasNoComposer") = []
{
    const auto keyboardUp =
        Insets {.top = iPhone17eSafeAreaTop, .bottom = measuredIPhone17eKeyboardHeight};
    const auto rects = calculateTypingRects(iPhone17eBounds, keyboardUp, false);

    check(rects.composer.h == 0.0f);
    check(rects.pane.bottom() == rects.keyRow.y);
};
