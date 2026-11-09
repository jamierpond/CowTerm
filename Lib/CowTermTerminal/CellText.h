#pragma once

#include "GridSelection.h"

#include "CowTermCore/TermScreen.h"

#include <string>

// Reading text back out of the cell grid: selections, debug dumps and the word
// a double-click lands on.
namespace term
{
// The line at an absolute row (see CellRef), scrollback or grid.
const Line& findLineAtAbsoluteRow(const TermScreen& screen, long row);

long findLastAbsoluteRow(const TermScreen& screen);

// Columns up to the last non-blank cell.
int measureInkLength(const Line& line);

// Every cell's character up to `columns`, trailing blanks trimmed.
std::string extractVisibleRowText(const TermScreen& screen, const Line& line, int columns);

// The text from `start` to `end` inclusive, one line per row, each trimmed of
// trailing blanks. Rows outside the buffer are skipped.
std::string extractTextBetween(const TermScreen& screen, CellRef start, CellRef end);

struct WordSpan
{
    int firstColumn = 0;
    int lastColumn = 0;

    // False when the column itself is blank: then there is no word to select.
    bool isOnWord = false;
};

// The run of non-blank cells around `column`.
WordSpan findWordAround(const Line& line, int column);
} // namespace term
