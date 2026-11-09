#include "Reflow.h"

#include <algorithm>

namespace term
{
namespace
{
constexpr auto wideCharacterWidth = 2;

bool isBlank(const Cell& cell)
{
    constexpr auto defaultColors = (std::uint16_t) (Attr::DefaultFg | Attr::DefaultBg);
    return cell.ch == U' ' && cell.attrs == defaultColors && cell.hyperlink == 0;
}

// One line as the program wrote it, before the screen cut it into rows.
struct LogicalLine
{
    std::vector<Cell> cells;
    long cursorOffset = -1;
};

void trimTrailingBlanks(LogicalLine& line)
{
    const auto keep = (std::size_t) std::max(line.cursorOffset + 1, 0L);

    while (line.cells.size() > keep && isBlank(line.cells.back()))
        line.cells.pop_back();
}

std::vector<LogicalLine> joinWrappedRows(const std::vector<Line>& rows, long cursorRow, int cursorColumn)
{
    auto logical = std::vector<LogicalLine> {1};

    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        auto& line = logical.back();

        if ((long) index == cursorRow)
            line.cursorOffset = (long) line.cells.size() + cursorColumn;

        for (const auto& cell: rows[index])
        {
            if ((cell.attrs & Attr::WrapSpacer) == 0)
                line.cells.push_back(cell);
        }

        if (!rows[index].wrapped)
        {
            trimTrailingBlanks(line);
            logical.emplace_back();
        }
    }

    if (logical.back().cells.empty() && logical.size() > 1)
        logical.pop_back();

    return logical;
}

class RowCutter
{
public:
    RowCutter(int columnsToUse, const Cell& blankToUse, ReflowedLines& outputToUse)
        : columns(columnsToUse)
        , blank(blankToUse)
        , output(outputToUse)
    {
    }

    void cut(const LogicalLine& line)
    {
        startRow();

        for (std::size_t index = 0; index < line.cells.size(); ++index)
        {
            place(line.cells[index]);

            if ((long) index == line.cursorOffset)
                placeCursorOnLastCell();
        }

        if (line.cursorOffset >= (long) line.cells.size())
            placeCursorAfterLastCell();

        padRow();
    }

private:
    void startRow()
    {
        output.lines.emplace_back();
        output.lines.back().reserve((std::size_t) columns);
    }

    void padRow()
    {
        output.lines.back().resize((std::size_t) columns, blank);
    }

    void wrapRow()
    {
        padRow();
        output.lines.back().wrapped = true;
        startRow();
    }

    bool isLastColumn(int column) const
    {
        return column == columns - 1 && columns >= wideCharacterWidth;
    }

    void place(const Cell& cell)
    {
        const auto isWide = (cell.attrs & Attr::Wide) != 0;
        const auto used = (int) output.lines.back().size();

        if ((cell.attrs & Attr::WideCont) != 0 && used == 0)
            return;

        if (isWide && isLastColumn(used))
        {
            auto spacer = blank;
            spacer.attrs |= Attr::WrapSpacer;
            output.lines.back().push_back(spacer);
            wrapRow();
        }
        else if (used == columns)
        {
            wrapRow();
        }

        output.lines.back().push_back(cell);
    }

    void placeCursorOnLastCell()
    {
        output.cursorRow = (long) output.lines.size() - 1;
        output.cursorColumn = (int) output.lines.back().size() - 1;
    }

    // Past the text: where the next cell would go, or the last column of a
    // full row, as with a pending wrap.
    void placeCursorAfterLastCell()
    {
        output.cursorRow = (long) output.lines.size() - 1;
        output.cursorColumn = std::min((int) output.lines.back().size(), columns - 1);
    }

    int columns;
    const Cell& blank;
    ReflowedLines& output;
};

bool isBlankUnwrappedRow(const Line& row)
{
    return !row.wrapped && std::all_of(row.begin(), row.end(), isBlank);
}

void dropBlankRowsBelowCursor(ReflowedLines& reflowed)
{
    auto& lines = reflowed.lines;

    while ((long) lines.size() - 1 > reflowed.cursorRow && isBlankUnwrappedRow(lines.back()))
        lines.pop_back();
}
} // namespace

ReflowedLines reflowLines(const ReflowRequest& request)
{
    auto reflowed = ReflowedLines {};
    auto cutter = RowCutter {request.newColumns, request.blank, reflowed};

    for (const auto& line: joinWrappedRows(request.rows, request.cursorRow, request.cursorColumn))
        cutter.cut(line);

    dropBlankRowsBelowCursor(reflowed);
    return reflowed;
}
} // namespace term
