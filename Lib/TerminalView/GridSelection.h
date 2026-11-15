#pragma once

namespace term
{
// A cell anywhere in the buffer. Absolute row: scrollback lines count from 0,
// grid rows follow.
struct CellRef
{
    long row = 0;
    int col = 0;

    bool operator<(const CellRef& other) const
    {
        return row != other.row ? row < other.row : col < other.col;
    }

    bool operator==(const CellRef& other) const
    {
        return row == other.row && col == other.col;
    }
};

// A cell on screen: a row of the viewport rather than of the buffer.
struct VisibleCell
{
    int visualRow = 0;
    int column = 0;
};

// The highlighted span, inclusive at both ends, in reading order.
struct SelectionRange
{
    CellRef start;
    CellRef end;
    bool active = false;

    bool contains(CellRef cell) const
    {
        if (!active)
            return false;

        return !(cell < start) && !(end < cell);
    }
};
} // namespace term
