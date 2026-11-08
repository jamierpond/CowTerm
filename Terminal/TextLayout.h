#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace term
{
using TextWidthMeasurer = std::function<float(const std::string& text)>;

// Every piece between separators, empty ones included; text without a
// separator is one piece.
std::vector<std::string> splitAtSeparator(const std::string& text, char separator);

bool isUtf8ContinuationByte(char byte);
std::size_t findPreviousCodepointStart(const std::string& text, std::size_t end);
std::size_t findNextCodepointEnd(const std::string& text, std::size_t start);
void eraseLastCodepoint(std::string& text);

// Whether a key's characters are text to type rather than a control code.
bool isPrintableTypedText(const std::string& characters);

// Cuts by bytes, so the result can end mid-codepoint before the ellipsis.
std::string truncateWithEllipsis(const std::string& text, std::size_t maximumBytes);

std::string truncateTextToWidth(const std::string& source,
                                float width,
                                const TextWidthMeasurer& measureWidth);

// Word-wraps each line of source to width; a word wider than a whole line
// breaks between codepoints.
std::vector<std::string> wrapTextToWidth(const std::string& source,
                                         float width,
                                         const TextWidthMeasurer& measureWidth);
} // namespace term
