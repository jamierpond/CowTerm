#pragma once

#include "PaneScreenInputs.h"

#include <eacp/Graphics/Primitives/Primitives.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Where the pane screen's parts go and what a finger on them means. Plain
// maths, so the Mac's tests can check it.
namespace cowterm::mobile
{
using eacp::Graphics::Insets;
using eacp::Graphics::Point;
using eacp::Graphics::Rect;

inline constexpr auto topLineHeight = 28.0f;
inline constexpr auto bottomBarHeight = 52.0f;
inline constexpr auto keyRowHeight = 48.0f;
inline constexpr auto composerHeight = 44.0f;
inline constexpr auto promptButtonHeight = 40.0f;
inline constexpr auto stopButtonWidth = 52.0f;

// The iPhone 17e's keyboard in portrait with its suggestion bar, as the
// composer brings it up, measured in the simulator: the keyboard-up row count
// until the real keyboard has shown once.

struct PaneScreenRects
{
    Rect topLine;
    Rect pane;
    Rect bottomBar;
};

// Top line under the status bar, bottom bar above the home indicator, and the
// pane between them, all inside the safe area.
PaneScreenRects calculatePaneScreenRects(Rect bounds, Insets safeArea);

struct TypingRects
{
    Rect topLine;
    Rect pane;
    Rect composer;
    Rect keyRow;
};

// With the keyboard up: the top line, the pane, then the composer (none in raw
// mode) and the key row sitting on the keyboard. insetsAroundParts has the
// keyboard's height as its bottom.
TypingRects calculateTypingRects(Rect bounds, Insets insetsAroundParts, bool showsComposer);

// The pill's place: centred, just above the bottom of the pane.
Rect calculateNewLinesPillBounds(Rect pane, float pillWidth);

enum class BottomBarControl
{
    None,
    PreviousSession,
    PaneDot,
    Keyboard,
    Git,
    Overview
};

struct BottomBarLayout
{
    std::optional<Rect> previousSession;
    std::vector<Rect> paneDots;
    Rect keyboard;
    Rect git;
    Rect overview;
};

// previousSessionWidth is the chip's measured width, or nothing when there is
// no previous session. A one-pane session has no dots.
BottomBarLayout calculateBottomBarLayout(Rect bar,
                                         std::optional<float> previousSessionWidth,
                                         int paneCount);

struct BottomBarHit
{
    BottomBarControl control = BottomBarControl::None;
    int paneIndex = -1;
};

BottomBarHit findBottomBarControlAt(const BottomBarLayout& layout, Point position);

enum class BottomBarGesture
{
    Tap,
    SwipeToNextPane,
    SwipeToPreviousPane,
    SwipeUp,
    Unclear
};

// What a finger that went down at start and lifted at end did on the bar. A
// long press is decided by time while the finger is still down, not here.
BottomBarGesture classifyBottomBarRelease(Point start, Point end);

// Whether a finger has moved far enough from where it went down to be a drag
// rather than a tap or a long press.
bool hasMovedBeyondTapSlop(Point start, Point current);

// A finger that started on the screen's left edge and travelled right: the
// iOS back gesture, which takes the pane screen back to the overview.
bool isStartingAtTheLeftEdge(Point start);
bool isSwipeFromTheLeftEdge(Point start, Point end);

// A finger on the bar below its row of controls, over the home indicator,
// means the control above it.
Point moveOntoTheBarsRow(Point position);

// The pane after a sideways swipe or a dot tap, kept within the session.
int choosePaneAfterSwipe(int activePane, int paneCount, BottomBarGesture gesture);

// "● live", "connecting · 12 s ago", "stalled · 3 min ago", "unreachable · …": the top line's
// right end. A pane that is not live says how old its picture is.
std::string describeLinkStatus(LinkState state, std::int64_t pictureAgeMilliseconds);

// "macprorack/Grazer": the host's label, then the session when it has a name.
std::string describePaneTitle(const std::string& host, const std::string& sessionName);

// "↓ 214 new"
std::string formatNewLinesPill(std::uint64_t lineCount);
} // namespace cowterm::mobile
