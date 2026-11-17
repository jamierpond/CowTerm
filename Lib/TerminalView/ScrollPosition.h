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

enum class ScrollEdgeContact
{
    None,
    StoppedAtEdge
};

// Where the reader is in the scrollback: the absolute row at the top of the
// view (the numbering CellRef uses) and how much of that row is scrolled out
// of sight. At the live bottom it follows new output; scrolled back it holds
// still while output arrives, and counts the lines that arrived below it.
class ScrollPosition
{
public:
    bool isFollowingLiveBottom() const { return followsLiveBottom; }
    void followLiveBottom();

    // Positive rows move toward older lines, as a wheel turned up does.
    ScrollEdgeContact scrollByRows(double rows, int scrollbackSize);
    void showLinesFromBottom(int linesFromBottom, int scrollbackSize);

    // Takes the screen's current totals; lines dropped off the full ring shift
    // the anchored row up with the text it shows.
    void applyScrollbackTotals(ScrollbackTotals totals, int scrollbackSize);

    long findTopAbsoluteRow(int scrollbackSize) const;
    int calculateLinesFromBottom(int scrollbackSize) const;

    // Between 0 and 1: the part of the top row scrolled above the view.
    double getHiddenFractionOfTopRow() const { return hiddenFractionOfTopRow; }
    std::uint64_t countLinesArrivedWhileScrolledBack() const
    {
        return linesArrivedWhileScrolledBack;
    }

private:
    void moveToRowPosition(double rowPosition, int scrollbackSize);

    long topRow = 0;
    double hiddenFractionOfTopRow = 0.0;
    bool followsLiveBottom = true;
    std::uint64_t linesArrivedWhileScrolledBack = 0;
    ScrollbackTotals totalsSeen;
};
} // namespace term
