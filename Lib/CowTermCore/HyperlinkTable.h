#pragma once

#include "InternTable.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace term
{
using HyperlinkId = std::uint16_t;
inline constexpr HyperlinkId noHyperlink = 0;
inline constexpr std::size_t largestHyperlinkCount = 4096;

// The URIs of OSC 8 hyperlinks, each stored once; cells hold the small id.
using HyperlinkTable = InternTable<std::string, largestHyperlinkCount>;

// True for the schemes a click may hand to the system opener: http, https,
// mailto and file. Anything else (a custom app scheme) is never opened.
bool hasOpenableScheme(std::string_view uri);
} // namespace term
