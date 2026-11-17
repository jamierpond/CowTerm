#pragma once

#include "GridSelection.h"

#include "Emulator/TermScreen.h"

#include <string>

// Reading text back out of the cell grid: selections, debug dumps and the word
// a double-click lands on.
namespace term
{
// The line at an absolute row (see CellRef), scrollback or grid.
const Line& findLineAtAbsoluteRow(const TermScreen& screen, long row);

// The active screen's first row: the oldest scrollback line, or under a
// full-screen program the top of its screen, which has no history.
long findFirstAbsoluteRow(const TermScreen& screen);

long findLastAbsoluteRow(const TermScreen& screen);

bool isAbsoluteRowStored(const TermScreen& screen, long row);

// The row is soft-wrapped and the row it runs on into is in the buffer.
bool doesRowRunOntoNextRow(const TermScreen& screen, long row);

struct RowSpan
{
    long first = 0;
    long last = 0;
};

// The rows one soft-wrapped line covers.
RowSpan findSoftWrappedLineRows(const TermScreen& screen, long row);

// The cell's character, or its whole grapheme cluster, as UTF-8.
void appendCellText(std::string& out, const TermScreen& screen, const Cell& cell);

// Columns up to the last non-blank cell.
int measureInkLength(const Line& line);

// Every cell's character up to `columns`, trailing blanks trimmed.
std::string extractVisibleRowText(const TermScreen& screen, const Line& line, int columns);

// The text from `start` to `end` inclusive. A soft-wrapped row runs straight
// on into the next; other rows end in a newline, trimmed of trailing blanks.
// Rows outside the buffer are skipped.
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

// The run of non-blank cells around `cell`, carried across soft wraps; inactive
// when the cell is blank.
SelectionRange findWrappedWordAround(const TermScreen& screen, CellRef cell);
} // namespace term
