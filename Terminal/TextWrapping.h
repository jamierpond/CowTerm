#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace term
{
using TextWidthMeasurer = std::function<float(std::string_view)>;

// Breaks at spaces; a word wider than the line is broken between characters.
// Past maximumLineCount the last kept line ends in an ellipsis.
std::vector<std::string> wrapTextToWidth(std::string_view text,
                                         float maximumWidth,
                                         int maximumLineCount,
                                         const TextWidthMeasurer& measureWidth);
} // namespace term
