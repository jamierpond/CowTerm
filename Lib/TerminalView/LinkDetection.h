#pragma once

#include "GridSelection.h"

#include "Emulator/TermScreen.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Finding the link under a cell: an OSC 8 hyperlink, or a URL written as plain
// text, either one running across soft-wrapped rows. A text URL also runs on
// into an indented row when a program broke the line itself, as Claude Code does.
namespace term
{
// Cells of a link, inclusive at both ends, in reading order.
struct LinkSpan
{
    CellRef start;
    CellRef end;

    // Where each later row's part of the link begins, for rows that start
    // with an indent; the indent cells are not part of the link.
    std::vector<CellRef> indentedRowStarts;

    bool contains(CellRef cell) const;
};

struct DetectedLink
{
    LinkSpan span;
    std::string uri;
};

// Byte range [begin, end) of text.
struct TextRange
{
    std::size_t begin = 0;
    std::size_t end = 0;
};

// The openable URL in text covering byteIndex, with trailing sentence
// punctuation and unmatched closing brackets left off.
std::optional<TextRange> findUrlAround(std::string_view text, std::size_t byteIndex);

std::optional<DetectedLink> findLinkAt(const TermScreen& screen, CellRef cell);
} // namespace term
