#pragma once

#include "GridSelection.h"

#include "CowTermCore/TermScreen.h"

#include <optional>
#include <string>
#include <string_view>

// Finding the link under a cell: an OSC 8 hyperlink, or a URL written as plain
// text, either one running across soft-wrapped rows.
namespace term
{
// Cells of a link, inclusive at both ends, in reading order.
struct LinkSpan
{
    CellRef start;
    CellRef end;

    bool contains(CellRef cell) const { return !(cell < start) && !(end < cell); }
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
