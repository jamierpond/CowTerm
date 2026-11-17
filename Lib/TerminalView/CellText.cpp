#include "CellText.h"

#include <eacp/Text/Utf8.h>

#include <algorithm>
#include <optional>

namespace term
{
namespace
{
constexpr auto longestUtf8Sequence = 4;

void appendUtf8(std::string& out, char32_t codepoint)
{
    char encoded[longestUtf8Sequence];
    out.append(encoded, (std::size_t) eacp::Text::encodeUtf8(codepoint, encoded));
}

void removeTrailingSpaces(std::string& text)
{
    while (!text.empty() && text.back() == ' ')
        text.pop_back();
}

bool isRightHalfOfWideCharacter(const Line& line, int column)
{
    return column > 0 && column < (int) line.size()
           && line[(std::size_t) column].hasFlag(CellFlag::WideCont);
}

std::string extractRowText(const TermScreen& screen, const Line& line, int fromCol, int toCol)
{
    auto text = std::string {};
    const auto lastCol = std::min(toCol, (int) line.size() - 1);

    if (isRightHalfOfWideCharacter(line, fromCol))
        --fromCol;

    for (auto col = fromCol; col <= lastCol; ++col)
    {
        const auto& cell = line[(std::size_t) col];

        if (!cell.hasFlag(CellFlag::WideCont))
            appendCellText(text, screen, cell);
    }

    return text;
}

bool isWordCharacterAt(const Line& line, int column)
{
    if (column < 0 || column >= (int) line.size())
        return false;

    return !line[(std::size_t) column].isSpace();
}
} // namespace

void appendCellText(std::string& out, const TermScreen& screen, const Cell& cell)
{
    const auto resolved = screen.resolveCell(cell);

    if (resolved.text.empty())
    {
        appendUtf8(out, resolved.ch);
        return;
    }

    for (const auto codepoint: resolved.text)
        appendUtf8(out, codepoint);
}

// Grid rows sit past the scrollback and resolve to negative offsets, down to
// -(rows - 1) for the bottom line.
bool isAbsoluteRowStored(const TermScreen& screen, long row)
{
    const auto offset = (int) (screen.scrollbackSize() - row);
    return offset > -screen.rows() && offset <= screen.scrollbackSize();
}

bool doesRowRunOntoNextRow(const TermScreen& screen, long row)
{
    return isAbsoluteRowStored(screen, row) && findLineAtAbsoluteRow(screen, row).wrapped
           && isAbsoluteRowStored(screen, row + 1);
}

RowSpan findSoftWrappedLineRows(const TermScreen& screen, long row)
{
    auto rows = RowSpan {row, row};

    while (doesRowRunOntoNextRow(screen, rows.first - 1))
        --rows.first;

    while (doesRowRunOntoNextRow(screen, rows.last))
        ++rows.last;

    return rows;
}

namespace
{
// Where the word that `wordStart` continues begins, when `wordStart` opens a
// row that the row above soft-wraps into and that row ends in a word.
std::optional<CellRef> findWordEndingAboveTheWrap(const TermScreen& screen, CellRef wordStart)
{
    const auto above = wordStart.row - 1;

    if (wordStart.col != 0 || !doesRowRunOntoNextRow(screen, above))
        return std::nullopt;

    const auto& line = findLineAtAbsoluteRow(screen, above);
    const auto word = findWordAround(line, (int) line.size() - 1);

    if (!word.isOnWord)
        return std::nullopt;

    return CellRef {above, word.firstColumn};
}

// Where the word that `wordEnd` runs on into ends, when `wordEnd` closes a
// soft-wrapped row and the next row starts with a word.
std::optional<CellRef> findWordStartingBelowTheWrap(const TermScreen& screen, CellRef wordEnd)
{
    const auto& line = findLineAtAbsoluteRow(screen, wordEnd.row);
    const auto isAtRowEnd = wordEnd.col == (int) line.size() - 1;

    if (!isAtRowEnd || !doesRowRunOntoNextRow(screen, wordEnd.row))
        return std::nullopt;

    const auto word = findWordAround(findLineAtAbsoluteRow(screen, wordEnd.row + 1), 0);

    if (!word.isOnWord)
        return std::nullopt;

    return CellRef {wordEnd.row + 1, word.lastColumn};
}

// A soft-wrapped row runs straight on, keeping a space that ended it.
void appendSelectedRowText(const TermScreen& screen,
                           long row,
                           const SelectionRange& selected,
                           std::string& result)
{
    const auto& line = findLineAtAbsoluteRow(screen, row);
    const auto isLastRow = row == selected.end.row;
    const auto fromCol = row == selected.start.row ? selected.start.col : 0;
    const auto toCol = isLastRow ? selected.end.col : (int) line.size() - 1;
    const auto runsOnToNextRow = line.wrapped && !isLastRow;

    auto rowText = extractRowText(screen, line, fromCol, toCol);

    if (!runsOnToNextRow)
        removeTrailingSpaces(rowText);

    result += rowText;

    if (!isLastRow && !runsOnToNextRow)
        result.push_back('\n');
}
} // namespace

const Line& findLineAtAbsoluteRow(const TermScreen& screen, long row)
{
    return screen.lineAt(0, (int) ((long) screen.scrollbackSize() - row));
}

long findLastAbsoluteRow(const TermScreen& screen)
{
    return (long) screen.scrollbackSize() + screen.rows() - 1;
}

int measureInkLength(const Line& line)
{
    auto length = (int) line.size();

    while (length > 0 && line[(std::size_t) length - 1].isSpace())
        --length;

    return length;
}

std::string extractVisibleRowText(const TermScreen& screen, const Line& line, int columns)
{
    auto text = std::string {};
    const auto lastCol = std::min(columns, (int) line.size());

    for (int col = 0; col < lastCol; ++col)
        appendCellText(text, screen, line[(std::size_t) col]);

    removeTrailingSpaces(text);
    return text;
}

std::string extractTextBetween(const TermScreen& screen, CellRef start, CellRef end)
{
    auto result = std::string {};

    for (auto row = start.row; row <= end.row; ++row)
    {
        if (isAbsoluteRowStored(screen, row))
            appendSelectedRowText(screen, row, {start, end, true}, result);
    }

    return result;
}

WordSpan findWordAround(const Line& line, int column)
{
    auto span = WordSpan {column, column, isWordCharacterAt(line, column)};

    while (isWordCharacterAt(line, span.firstColumn - 1))
        --span.firstColumn;

    while (isWordCharacterAt(line, span.lastColumn + 1))
        ++span.lastColumn;

    return span;
}

SelectionRange findWrappedWordAround(const TermScreen& screen, CellRef cell)
{
    const auto word = findWordAround(findLineAtAbsoluteRow(screen, cell.row), cell.col);
    auto range = SelectionRange {
        {cell.row, word.firstColumn}, {cell.row, word.lastColumn}, word.isOnWord};

    if (!range.active)
        return range;

    while (const auto before = findWordEndingAboveTheWrap(screen, range.start))
        range.start = *before;

    while (const auto after = findWordStartingBelowTheWrap(screen, range.end))
        range.end = *after;

    return range;
}
} // namespace term
