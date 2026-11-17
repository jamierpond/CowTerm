#pragma once

#include <cstdint>

namespace term
{
// TermScreen's running counts of lines that entered and left the scrollback.
struct ScrollbackTotals
{
    std::uint64_t linesAdded = 0;
    std::uint64_t linesDropped = 0;
};

// What the view shows, in rows. The viewport can be taller than the grid the
// shell draws in (a phone with its keyboard down): the grid then sits at the
// bottom and the rows above it show scrollback.
struct ViewportLayout
{
    long scrollbackSize = 0;
    long gridRows = 0;
    long viewportRows = 0;

    // The alternate screen has no scrollback of its own to show.
    bool isScrollbackShown = true;
};

// The absolute rows (CellRef's numbering) the view's top row can be at.
// Rows below 0 are blank space above the oldest line.
struct ScrollRange
{
    long oldestTopRow = 0;
    long liveBottomTopRow = 0;
};

// At the live bottom the grid's last row is the viewport's last row; the
// oldest position puts scrollback line 0 at the top, unless the content is
// shorter than the viewport, and then nothing scrolls.
ScrollRange calculateScrollRange(const ViewportLayout& layout);

enum class ScrollEdgeContact
{
    None,
    StoppedAtEdge
};

// Where the reader is in the scrollback: the absolute row at the top of the
// view and how much of that row is scrolled out of sight. At the live bottom
// it follows new output; scrolled back it holds still while output arrives,
// and counts the lines that arrived below it.
class ScrollPosition
{
public:
    bool isFollowingLiveBottom() const { return followsLiveBottom; }
    void followLiveBottom();

    // Positive rows move toward older lines, as a wheel turned up does.
    ScrollEdgeContact scrollByRows(double rows, ScrollRange range);
    void showLinesFromBottom(int linesFromBottom, ScrollRange range);
    void showTopRow(long absoluteRow, ScrollRange range);

    // Takes the screen's current totals; lines dropped off the full ring shift
    // the anchored row up with the text it shows.
    void applyScrollbackTotals(ScrollbackTotals totals, ScrollRange range);

    long findTopAbsoluteRow(ScrollRange range) const;
    int calculateLinesFromBottom(ScrollRange range) const;

    // Between 0 and 1: the part of the top row scrolled above the view.
    double getHiddenFractionOfTopRow() const { return hiddenFractionOfTopRow; }
    std::uint64_t countLinesArrivedWhileScrolledBack() const
    {
        return linesArrivedWhileScrolledBack;
    }

    // Whether the view left or reached the live bottom, or the count changed,
    // since the last call; the call clears it.
    bool takeWhetherScrolledBackStateChanged();

private:
    void moveToRowPosition(double rowPosition, ScrollRange range);
    void setLinesArrivedWhileScrolledBack(std::uint64_t count);
    void setFollowsLiveBottom(bool follows);

    long topRow = 0;
    double hiddenFractionOfTopRow = 0.0;
    bool followsLiveBottom = true;
    std::uint64_t linesArrivedWhileScrolledBack = 0;
    bool scrolledBackStateChanged = false;
    ScrollbackTotals totalsSeen;
};
} // namespace term
