#include "TextLayout.h"

#include <algorithm>

namespace term
{
namespace
{
constexpr auto utf8ContinuationMask = 0xc0;
constexpr auto utf8ContinuationBits = 0x80;
constexpr auto firstPrintableCharacter = 0x20;
constexpr auto deleteCharacter = 0x7f;
constexpr auto ellipsis = "…";

// Takes the longest prefix of word that fits (at least one codepoint) off the
// front of word and returns it.
std::string takeWidestFittingPrefix(std::string& word,
                                    float width,
                                    const TextWidthMeasurer& measureWidth)
{
    auto piece = std::string {};
    auto cut = std::size_t {0};

    while (cut < word.size())
    {
        const auto next = findNextCodepointEnd(word, cut);

        if (!piece.empty() && measureWidth(word.substr(0, next)) > width)
            break;

        piece = word.substr(0, next);
        cut = next;
    }

    word.erase(0, cut);
    return piece;
}

void wrapParagraphToWidth(const std::string& paragraph,
                          float width,
                          const TextWidthMeasurer& measureWidth,
                          std::vector<std::string>& lines)
{
    const auto fits = [&](const std::string& candidate)
    { return measureWidth(candidate) <= width; };

    auto line = std::string {};
    auto start = std::size_t {0};

    while (start <= paragraph.size())
    {
        const auto end = std::min(paragraph.find(' ', start), paragraph.size());
        auto word = paragraph.substr(start, end - start);
        start = end + 1;

        const auto joined = line.empty() ? word : line + " " + word;

        if (fits(joined))
        {
            line = joined;
            continue;
        }

        if (!line.empty())
            lines.push_back(line);

        while (!fits(word))
            lines.push_back(takeWidestFittingPrefix(word, width, measureWidth));

        line = word;
    }

    lines.push_back(line);
}
} // namespace

std::vector<std::string> splitAtSeparator(const std::string& text, char separator)
{
    auto pieces = std::vector<std::string> {};
    auto start = std::size_t {0};

    while (true)
    {
        const auto end = text.find(separator, start);

        if (end == std::string::npos)
        {
            pieces.push_back(text.substr(start));
            return pieces;
        }

        pieces.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

bool isUtf8ContinuationByte(char byte)
{
    const auto value = (unsigned char) byte;
    return (value & utf8ContinuationMask) == utf8ContinuationBits;
}

std::size_t findPreviousCodepointStart(const std::string& text, std::size_t end)
{
    auto start = end;

    while (start > 0)
    {
        --start;

        if (!isUtf8ContinuationByte(text[start]))
            return start;
    }

    return 0;
}

std::size_t findNextCodepointEnd(const std::string& text, std::size_t start)
{
    auto end = start + 1;

    while (end < text.size() && isUtf8ContinuationByte(text[end]))
        ++end;

    return end;
}

void eraseLastCodepoint(std::string& text)
{
    text.erase(findPreviousCodepointStart(text, text.size()));
}

bool isPrintableTypedText(const std::string& characters)
{
    if (characters.empty())
        return false;

    const auto firstByte = (unsigned char) characters[0];
    return firstByte >= firstPrintableCharacter && firstByte != deleteCharacter;
}

std::string truncateWithEllipsis(const std::string& text, std::size_t maximumBytes)
{
    if (text.size() <= maximumBytes)
        return text;

    return text.substr(0, maximumBytes - 1) + ellipsis;
}

std::string truncateTextToWidth(const std::string& source,
                                float width,
                                const TextWidthMeasurer& measureWidth)
{
    if (measureWidth(source) <= width)
        return source;

    auto truncated = source;

    while (!truncated.empty() && measureWidth(truncated + ellipsis) > width)
        truncated.erase(findPreviousCodepointStart(truncated, truncated.size()));

    return truncated + ellipsis;
}

std::vector<std::string> wrapTextToWidth(const std::string& source,
                                         float width,
                                         const TextWidthMeasurer& measureWidth)
{
    auto lines = std::vector<std::string> {};

    for (const auto& paragraph: splitAtSeparator(source, '\n'))
        wrapParagraphToWidth(paragraph, width, measureWidth, lines);

    return lines;
}
} // namespace term
