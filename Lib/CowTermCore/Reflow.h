#pragma once

#include "TermScreen.h"

#include <vector>

namespace term
{
struct ReflowedLines
{
    std::vector<Line> lines;

    // The cursor's row in lines and its column.
    long cursorRow = 0;
    int cursorColumn = 0;
};

// Rejoins rows that soft-wrapped into the lines the program wrote, then cuts
// them again at newColumns, as Ghostty's reflow does. Blank cells at the end
// of a line are dropped, but never the cell under the cursor, which keeps its
// place in the text. Blank rows below the cursor are dropped too; the caller
// pads the screen back out.
struct ReflowRequest
{
    const std::vector<Line>& rows;
    long cursorRow = 0;
    int cursorColumn = 0;
    int newColumns = 0;

    // Fills the new rows' unused cells.
    Cell blank;
};

ReflowedLines reflowLines(const ReflowRequest& request);
} // namespace term
