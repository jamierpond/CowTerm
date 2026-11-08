#include "CellText.h"

#include <eacp/Text/Utf8.h>

#include <algorithm>

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

bool isWideContinuation(const Cell& cell)
{
    return (cell.attrs & Attr::WideCont) != 0;
}

std::string extractRowText(const Line& line, int fromCol, int toCol)
{
    auto text = std::string {};
    const auto lastCol = std::min(toCol, (int) line.size() - 1);

    for (auto col = fromCol; col <= lastCol; ++col)
    {
        const auto& cell = line[(std::size_t) col];

        if (!isWideContinuation(cell))
            appendUtf8(text, cell.ch);
    }

    removeTrailingSpaces(text);
    return text;
}

// Grid rows sit past the scrollback and resolve to negative offsets, down to
// -(rows - 1) for the bottom line.
bool isAbsoluteRowStored(const TermScreen& screen, long row)
{
    const auto offset = (int) (screen.scrollbackSize() - row);
    return offset > -screen.rows() && offset <= screen.scrollbackSize();
}

bool isWordCharacterAt(const Line& line, int column)
{
    if (column < 0 || column >= (int) line.size())
        return false;

    return line[(std::size_t) column].ch != U' ';
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

    while (length > 0 && line[(std::size_t) length - 1].ch == U' ')
        --length;

    return length;
}

std::string extractVisibleRowText(const Line& line, int columns)
{
    auto text = std::string {};
    const auto lastCol = std::min(columns, (int) line.size());

    for (int col = 0; col < lastCol; ++col)
        appendUtf8(text, line[(std::size_t) col].ch);

    removeTrailingSpaces(text);
    return text;
}

std::string extractTextBetween(const TermScreen& screen, CellRef start, CellRef end)
{
    auto result = std::string {};

    for (auto row = start.row; row <= end.row; ++row)
    {
        if (!isAbsoluteRowStored(screen, row))
            continue;

        const auto& line = findLineAtAbsoluteRow(screen, row);
        const auto fromCol = row == start.row ? start.col : 0;
        const auto toCol = row == end.row ? end.col : (int) line.size() - 1;

        result += extractRowText(line, fromCol, toCol);

        if (row != end.row)
            result.push_back('\n');
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
} // namespace term
