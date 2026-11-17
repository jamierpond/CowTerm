#include "LinkDetection.h"

#include "CellText.h"

#include "Emulator/HyperlinkTable.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

namespace term
{
namespace
{
// A logical line stops growing here, so a screen of one endless wrapped line
// costs a bounded scan per pointer move.
constexpr auto longestLogicalLineRows = 64L;

constexpr auto urlPunctuation = std::string_view {"-._~:/?#[]@!$&()*+,;=%"};
constexpr auto trailingSentencePunctuation = std::string_view {".,;:!?"};
constexpr auto closingBrackets = std::string_view {")]"};
constexpr auto firstNonAsciiByte = 0x80;

// Claude Code's prompt box breaks a URL one column short of the right edge.
constexpr auto mostColumnsABrokenRowStopsShortOfTheEdge = 1;
constexpr auto deepestContinuationIndent = 8;
constexpr auto listBullets =
    std::array<std::string_view, 4> {"-", "*", "+", "\u2022"};
constexpr auto orderedListMarkerEndings = std::string_view {".)"};

enum class RowJoining
{
    SoftWrapsOnly,
    SoftWrapsAndBrokenUrls
};

struct LogicalCell
{
    CellRef ref;
    HyperlinkId hyperlink = noHyperlink;
};

// The soft-wrapped rows holding a cell, joined: their text, and for each byte
// of it the cell it came from.
struct LogicalLine
{
    std::vector<LogicalCell> cells;
    std::string text;
    std::vector<CellRef> cellOfByte;
};

bool isUrlByte(char byte)
{
    const auto code = (unsigned char) byte;

    if (code >= firstNonAsciiByte)
        return true;

    return std::isalnum(code) != 0
           || urlPunctuation.find(byte) != std::string_view::npos;
}

int measureIndent(const Line& line)
{
    auto indent = 0;

    while (indent < (int) line.size() && line[(std::size_t) indent].isSpace())
        ++indent;

    return indent;
}

std::string readTextOfCell(const TermScreen& screen, const Line& line, int column)
{
    auto text = std::string {};
    appendCellText(text, screen, line[(std::size_t) column]);
    return text;
}

std::string readWordFrom(const TermScreen& screen, const Line& line, int column)
{
    auto word = std::string {};

    for (; column < (int) line.size() && !line[(std::size_t) column].isSpace();
         ++column)
    {
        if (!line[(std::size_t) column].hasFlag(CellFlag::WideCont))
            word += readTextOfCell(screen, line, column);
    }

    return word;
}

bool isListMarker(std::string_view word)
{
    if (std::find(listBullets.begin(), listBullets.end(), word) != listBullets.end())
        return true;

    if (word.size() < 2
        || orderedListMarkerEndings.find(word.back()) == std::string_view::npos)
        return false;

    const auto number = word.substr(0, word.size() - 1);
    return std::all_of(number.begin(),
                       number.end(),
                       [](char digit)
                       { return std::isdigit((unsigned char) digit) != 0; });
}

bool isByteThatEndsTextRatherThanAUrl(char byte)
{
    return trailingSentencePunctuation.find(byte) != std::string_view::npos
           || closingBrackets.find(byte) != std::string_view::npos;
}

// Where a program that breaks its own lines may have cut a URL in two.
bool doesRowStopMidUrlAtTheEdge(const TermScreen& screen, const Line& line)
{
    const auto inkLength = measureInkLength(line);

    if (inkLength == 0
        || inkLength < screen.columns() - mostColumnsABrokenRowStopsShortOfTheEdge)
        return false;

    const auto lastCharacter = readTextOfCell(screen, line, inkLength - 1);
    return isUrlByte(lastCharacter.back())
           && !isByteThatEndsTextRatherThanAUrl(lastCharacter.back());
}

// A program that wraps text cuts a word only when the word is wider than the
// row, so a row holding one word from its first column to the edge carries on.
bool isRowOneWordToTheEdge(const TermScreen& screen, const Line& line)
{
    const auto inkLength = measureInkLength(line);

    if (inkLength < screen.columns() - mostColumnsABrokenRowStopsShortOfTheEdge)
        return false;

    return std::none_of(line.begin(),
                        line.begin() + inkLength,
                        [](const Cell& cell) { return cell.isSpace(); });
}

bool isContinuationIndent(int indent)
{
    return indent >= 1 && indent <= deepestContinuationIndent;
}

bool isAsciiUrlByte(char byte)
{
    return (unsigned char) byte < firstNonAsciiByte && isUrlByte(byte);
}

// From the column on, the row starts with URL text that is not a new URL or a
// list item; box drawing and other non-ASCII text does not carry a URL on.
bool doesRowContinueAUrlFrom(const TermScreen& screen, const Line& line, int column)
{
    if (column >= (int) line.size())
        return false;

    const auto word = readWordFrom(screen, line, column);
    return !word.empty() && isAsciiUrlByte(word.front()) && !isListMarker(word)
           && !hasOpenableScheme(word);
}

bool doesRowBreakWhereNextRowCarriesOn(const TermScreen& screen,
                                       const Line& line,
                                       int nextRowIndent)
{
    if (isContinuationIndent(nextRowIndent))
        return doesRowStopMidUrlAtTheEdge(screen, line);

    return nextRowIndent == 0 && isRowOneWordToTheEdge(screen, line);
}

// A program broke the row itself, so it is not marked wrapped, partway
// through a URL that the next row carries on.
bool doesRowContinueOnBrokenUrlRow(const TermScreen& screen, long row)
{
    if (!isAbsoluteRowStored(screen, row) || !isAbsoluteRowStored(screen, row + 1))
        return false;

    const auto& line = findLineAtAbsoluteRow(screen, row);
    const auto& next = findLineAtAbsoluteRow(screen, row + 1);
    const auto nextRowIndent = measureIndent(next);

    return !line.wrapped && doesRowBreakWhereNextRowCarriesOn(screen, line, nextRowIndent)
           && doesRowContinueAUrlFrom(screen, next, nextRowIndent);
}

bool doesRowJoinNextRow(const TermScreen& screen, long row, RowJoining joining)
{
    if (doesRowRunOntoNextRow(screen, row))
        return true;

    return joining == RowJoining::SoftWrapsAndBrokenUrls
           && doesRowContinueOnBrokenUrlRow(screen, row);
}

long findLogicalLineFirstRow(const TermScreen& screen, long row, RowJoining joining)
{
    auto first = row;

    while (row - first < longestLogicalLineRows
           && doesRowJoinNextRow(screen, first - 1, joining))
        --first;

    return first;
}

long findLogicalLineLastRow(const TermScreen& screen, long row, RowJoining joining)
{
    auto last = row;

    while (last - row < longestLogicalLineRows
           && doesRowJoinNextRow(screen, last, joining))
        ++last;

    return last;
}

// Columns [first, end) of a row.
struct ColumnRange
{
    int first = 0;
    int end = 0;
};

void appendRowToLogicalLine(const TermScreen& screen,
                            long row,
                            ColumnRange columns,
                            LogicalLine& logical)
{
    const auto& line = findLineAtAbsoluteRow(screen, row);

    for (auto col = columns.first; col < columns.end; ++col)
    {
        const auto& cell = line[(std::size_t) col];

        if (cell.hasFlag(CellFlag::WideCont))
            continue;

        const auto ref = CellRef {row, col};
        appendCellText(logical.text, screen, cell);
        logical.cellOfByte.resize(logical.text.size(), ref);
        logical.cells.push_back({ref, cell.hyperlink});
    }
}

// An indented continuation row is read from after its indent.
LogicalLine readLogicalLine(const TermScreen& screen, long row, RowJoining joining)
{
    auto logical = LogicalLine {};
    const auto last = findLogicalLineLastRow(screen, row, joining);
    auto firstColumn = 0;

    for (auto each = findLogicalLineFirstRow(screen, row, joining); each <= last;
         ++each)
    {
        const auto& line = findLineAtAbsoluteRow(screen, each);
        const auto isIndentedContinuationNext = each < last && !line.wrapped;
        auto columns = ColumnRange {firstColumn, (int) line.size()};
        firstColumn = 0;

        if (isIndentedContinuationNext)
        {
            columns.end = measureInkLength(line);
            firstColumn = measureIndent(findLineAtAbsoluteRow(screen, each + 1));
        }

        appendRowToLogicalLine(screen, each, columns, logical);
    }

    return logical;
}

std::optional<std::size_t> findCellIndex(const LogicalLine& logical, CellRef cell)
{
    const auto found = std::find_if(logical.cells.begin(),
                                    logical.cells.end(),
                                    [cell](const LogicalCell& each) { return each.ref == cell; });

    if (found == logical.cells.end())
        return std::nullopt;

    return (std::size_t) (found - logical.cells.begin());
}

std::optional<DetectedLink> findExplicitHyperlink(const TermScreen& screen,
                                                  const LogicalLine& logical,
                                                  std::size_t index)
{
    const auto id = logical.cells[index].hyperlink;
    const auto uri = screen.findHyperlinkUri(id);

    if (id == noHyperlink || !hasOpenableScheme(uri))
        return std::nullopt;

    auto first = index;
    auto last = index;

    while (first > 0 && logical.cells[first - 1].hyperlink == id)
        --first;

    while (last + 1 < logical.cells.size() && logical.cells[last + 1].hyperlink == id)
        ++last;

    return DetectedLink {{logical.cells[first].ref, logical.cells[last].ref, {}},
                         std::string {uri}};
}

LinkSpan makeTextUrlSpan(const LogicalLine& logical, TextRange range)
{
    auto span = LinkSpan {
        logical.cellOfByte[range.begin], logical.cellOfByte[range.end - 1], {}};
    auto previousRow = span.start.row;

    for (auto byte = range.begin; byte < range.end; ++byte)
    {
        const auto cell = logical.cellOfByte[byte];

        if (cell.row != previousRow && cell.col > 0)
            span.indentedRowStarts.push_back(cell);

        previousRow = cell.row;
    }

    return span;
}

std::optional<DetectedLink> findTextUrl(const LogicalLine& logical, CellRef cell)
{
    const auto firstByte = std::find(logical.cellOfByte.begin(), logical.cellOfByte.end(), cell);

    if (firstByte == logical.cellOfByte.end())
        return std::nullopt;

    const auto byteIndex = (std::size_t) (firstByte - logical.cellOfByte.begin());
    const auto range = findUrlAround(logical.text, byteIndex);

    if (!range)
        return std::nullopt;

    return DetectedLink {
        makeTextUrlSpan(logical, *range),
        logical.text.substr(range->begin, range->end - range->begin)};
}

long countByte(std::string_view text, char byte)
{
    return (long) std::count(text.begin(), text.end(), byte);
}

bool isUnmatchedCloser(std::string_view url, char closer, char opener)
{
    return url.back() == closer && countByte(url, closer) > countByte(url, opener);
}

bool shouldDropLastByte(std::string_view url)
{
    if (trailingSentencePunctuation.find(url.back()) != std::string_view::npos)
        return true;

    return isUnmatchedCloser(url, ')', '(') || isUnmatchedCloser(url, ']', '[');
}

std::optional<std::size_t> findLastSchemeStart(std::string_view text,
                                               std::size_t runBegin,
                                               std::size_t byteIndex)
{
    for (auto start = byteIndex + 1; start-- > runBegin;)
    {
        if (hasOpenableScheme(text.substr(start)))
            return start;
    }

    return std::nullopt;
}

TextRange findUrlByteRunAround(std::string_view text, std::size_t byteIndex)
{
    auto run = TextRange {byteIndex, byteIndex + 1};

    while (run.begin > 0 && isUrlByte(text[run.begin - 1]))
        --run.begin;

    while (run.end < text.size() && isUrlByte(text[run.end]))
        ++run.end;

    return run;
}

std::string_view trimUrlEnd(std::string_view url)
{
    while (!url.empty() && shouldDropLastByte(url))
        url.remove_suffix(1);

    return url;
}

std::size_t measureSchemeLength(std::string_view url)
{
    const auto colon = url.find(':');
    const auto afterColon = colon + 1;
    const auto slashes = url.find_first_not_of('/', afterColon);
    return slashes == std::string_view::npos ? url.size() : slashes;
}

std::vector<std::size_t> findSchemeStarts(std::string_view text, TextRange run)
{
    auto starts = std::vector<std::size_t> {};

    for (auto start = run.begin; start < run.end; ++start)
    {
        if (hasOpenableScheme(text.substr(start)))
            starts.push_back(start);
    }

    return starts;
}

// What findUrlAround finds for each byte of the run, in one pass: a byte
// belongs to the URL of the last scheme before it.
void addUrlsInRun(std::string_view text, TextRange run, std::vector<TextRange>& urls)
{
    const auto starts = findSchemeStarts(text, run);

    for (auto each = std::size_t {0}; each < starts.size(); ++each)
    {
        const auto start = starts[each];
        const auto url = trimUrlEnd(text.substr(start, run.end - start));
        const auto nextStart = each + 1 < starts.size() ? starts[each + 1] : run.end;

        if (url.size() > measureSchemeLength(url))
            urls.push_back({start, std::min(start + url.size(), nextStart)});
    }
}

std::vector<TextRange> findEveryUrlIn(std::string_view text)
{
    auto urls = std::vector<TextRange> {};
    auto byte = std::size_t {0};

    while (byte < text.size())
    {
        if (!isUrlByte(text[byte]))
        {
            ++byte;
            continue;
        }

        const auto run = findUrlByteRunAround(text, byte);
        addUrlsInRun(text, run, urls);
        byte = run.end;
    }

    return urls;
}

bool isOpenableHyperlinkCell(const TermScreen& screen, const Cell& cell)
{
    return cell.hyperlink != noHyperlink
           && hasOpenableScheme(screen.findHyperlinkUri(cell.hyperlink));
}

void markHyperlinkCells(const TermScreen& screen, const Line& line, std::vector<bool>& inLink)
{
    for (auto col = std::size_t {0}; col < line.size(); ++col)
    {
        if (isOpenableHyperlinkCell(screen, line[col]))
            inLink[col] = true;
    }
}

void markTextUrlCells(const LogicalLine& logical, long row, std::vector<bool>& inLink)
{
    for (const auto url: findEveryUrlIn(logical.text))
    {
        for (auto byte = url.begin; byte < url.end; ++byte)
        {
            const auto cell = logical.cellOfByte[byte];

            if (cell.row == row)
                inLink[(std::size_t) cell.col] = true;
        }
    }
}
} // namespace

bool LinkSpan::contains(CellRef cell) const
{
    if (cell < start || end < cell)
        return false;

    return std::none_of(indentedRowStarts.begin(),
                        indentedRowStarts.end(),
                        [cell](CellRef rowStart)
                        { return rowStart.row == cell.row && cell.col < rowStart.col; });
}

std::optional<TextRange> findUrlAround(std::string_view text, std::size_t byteIndex)
{
    if (byteIndex >= text.size() || !isUrlByte(text[byteIndex]))
        return std::nullopt;

    const auto run = findUrlByteRunAround(text, byteIndex);
    const auto start = findLastSchemeStart(text, run.begin, byteIndex);

    if (!start)
        return std::nullopt;

    const auto url = trimUrlEnd(text.substr(*start, run.end - *start));
    const auto end = *start + url.size();

    if (byteIndex >= end || url.size() <= measureSchemeLength(url))
        return std::nullopt;

    return TextRange {*start, end};
}

std::optional<DetectedLink> findLinkAt(const TermScreen& screen, CellRef cell)
{
    if (!isAbsoluteRowStored(screen, cell.row))
        return std::nullopt;

    const auto wrappedLine =
        readLogicalLine(screen, cell.row, RowJoining::SoftWrapsOnly);
    const auto index = findCellIndex(wrappedLine, cell);

    if (!index)
        return std::nullopt;

    if (wrappedLine.cells[*index].hyperlink != noHyperlink)
        return findExplicitHyperlink(screen, wrappedLine, *index);

    return findTextUrl(
        readLogicalLine(
            screen, cell.row, RowJoining::SoftWrapsAndBrokenUrls),
        cell);
}

std::optional<int> findColumnWhereBrokenUrlResumes(const TermScreen& screen, long row)
{
    if (!doesRowContinueOnBrokenUrlRow(screen, row))
        return std::nullopt;

    const auto lastCharacter =
        CellRef {row, measureInkLength(findLineAtAbsoluteRow(screen, row)) - 1};
    const auto link = findTextUrl(
        readLogicalLine(screen, row, RowJoining::SoftWrapsAndBrokenUrls), lastCharacter);

    if (!link || link->span.end.row == row)
        return std::nullopt;

    return measureIndent(findLineAtAbsoluteRow(screen, row + 1));
}

std::vector<bool> findCellsInLinksOnRow(const TermScreen& screen, long row)
{
    if (!isAbsoluteRowStored(screen, row))
        return {};

    const auto& line = findLineAtAbsoluteRow(screen, row);
    auto inLink = std::vector<bool>(line.size(), false);

    markHyperlinkCells(screen, line, inLink);
    markTextUrlCells(
        readLogicalLine(screen, row, RowJoining::SoftWrapsAndBrokenUrls), row, inLink);
    return inLink;
}
} // namespace term
