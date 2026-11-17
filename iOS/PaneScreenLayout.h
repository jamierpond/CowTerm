#pragma once

#include "PaneScreenInputs.h"

#include <eacp/Graphics/Primitives/Primitives.h>

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

// The iPhone 17e's keyboard in portrait, as eacp reported it in the simulator
// (the iPhone 18 Pro Max's is 320): the keyboard-up row count until the real
// keyboard has shown once.
inline constexpr auto defaultSoftwareKeyboardHeight = 301.0f;

struct PaneScreenRects
{
    Rect topLine;
    Rect pane;
    Rect bottomBar;
};

// Top line under the status bar, bottom bar above the home indicator, and the
// pane between them, all inside the safe area.
PaneScreenRects calculatePaneScreenRects(Rect bounds, Insets safeArea);

// The pill's place: centred, just above the bottom of the pane.
Rect calculateNewLinesPillBounds(Rect pane, float pillWidth);

struct KeyboardUpHeights
{
    float screenHeight = 0.0f;
    float safeAreaTop = 0.0f;
    float softwareKeyboardHeight = defaultSoftwareKeyboardHeight;
};

// The pane's height with the keyboard up: between the top line and the
// composer and key row. The pane keeps the rows this height holds with the
// keyboard down too, so showing the keyboard never resizes the shell's
// terminal.
float calculateKeyboardUpPaneHeight(const KeyboardUpHeights& heights);

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

// The pane after a sideways swipe or a dot tap, kept within the session.
int choosePaneAfterSwipe(int activePane, int paneCount, BottomBarGesture gesture);

// "12 s ago", "4 min ago", "2 h ago": how old the picture of a pane that is not
// live is.
std::string formatPictureAge(double seconds);

// "● live", "connecting · 12 s ago", "stalled · 3 min ago", "unreachable · …": the top line's
// right end. A pane that is not live says how old its picture is.
std::string describeLinkStatus(LinkState state, double pictureAgeSeconds);

// "↓ 214 new"
std::string formatNewLinesPill(std::uint64_t lineCount);
} // namespace cowterm::mobile
