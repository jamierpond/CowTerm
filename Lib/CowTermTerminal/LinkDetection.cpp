#include "LinkDetection.h"

#include "CellText.h"

#include "CowTermCore/HyperlinkTable.h"

#include <algorithm>
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
constexpr auto firstNonAsciiByte = 0x80;

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

bool isRowWrappedOnward(const TermScreen& screen, long row)
{
    return isAbsoluteRowStored(screen, row) && findLineAtAbsoluteRow(screen, row).wrapped;
}

bool doesRowRunOntoStoredRow(const TermScreen& screen, long row)
{
    return isRowWrappedOnward(screen, row) && isAbsoluteRowStored(screen, row + 1);
}

long findLogicalLineFirstRow(const TermScreen& screen, long row)
{
    auto first = row;

    while (row - first < longestLogicalLineRows && isRowWrappedOnward(screen, first - 1))
        --first;

    return first;
}

long findLogicalLineLastRow(const TermScreen& screen, long row)
{
    auto last = row;

    while (last - row < longestLogicalLineRows && doesRowRunOntoStoredRow(screen, last))
        ++last;

    return last;
}

void appendRowToLogicalLine(const TermScreen& screen, long row, LogicalLine& logical)
{
    const auto& line = findLineAtAbsoluteRow(screen, row);

    for (auto col = 0; col < (int) line.size(); ++col)
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

LogicalLine readLogicalLine(const TermScreen& screen, long row)
{
    auto logical = LogicalLine {};
    const auto last = findLogicalLineLastRow(screen, row);

    for (auto each = findLogicalLineFirstRow(screen, row); each <= last; ++each)
        appendRowToLogicalLine(screen, each, logical);

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

    return DetectedLink {{logical.cells[first].ref, logical.cells[last].ref}, std::string {uri}};
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

    const auto span = LinkSpan {logical.cellOfByte[range->begin], logical.cellOfByte[range->end - 1]};
    return DetectedLink {span, logical.text.substr(range->begin, range->end - range->begin)};
}

bool isUrlByte(char byte)
{
    const auto code = (unsigned char) byte;

    if (code >= firstNonAsciiByte)
        return true;

    return std::isalnum(code) != 0 || urlPunctuation.find(byte) != std::string_view::npos;
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
} // namespace

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

    const auto logical = readLogicalLine(screen, cell.row);
    const auto index = findCellIndex(logical, cell);

    if (!index)
        return std::nullopt;

    if (logical.cells[*index].hyperlink != noHyperlink)
        return findExplicitHyperlink(screen, logical, *index);

    return findTextUrl(logical, cell);
}
} // namespace term
