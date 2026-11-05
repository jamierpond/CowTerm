#include "TextWrapping.h"

namespace term
{
namespace
{
constexpr auto ellipsis = std::string_view {"…"};
constexpr auto utf8ContinuationMask = 0xC0u;
constexpr auto utf8ContinuationPattern = 0x80u;

bool isUtf8ContinuationByte(char character)
{
    auto byte = static_cast<unsigned char>(character);
    return (byte & utf8ContinuationMask) == utf8ContinuationPattern;
}

void removeLastCharacter(std::string& text)
{
    while (!text.empty() && isUtf8ContinuationByte(text.back()))
        text.pop_back();

    if (!text.empty())
        text.pop_back();
}

std::vector<std::string> splitIntoWords(std::string_view text)
{
    auto words = std::vector<std::string> {};
    auto current = std::string {};

    for (auto character: text)
    {
        auto isBreak = character == ' ' || character == '\n' || character == '\t';

        if (!isBreak)
        {
            current += character;
            continue;
        }

        if (!current.empty())
        {
            words.push_back(current);
            current.clear();
        }
    }

    if (!current.empty())
        words.push_back(current);

    return words;
}

std::vector<std::string> splitWordToWidth(const std::string& word,
                                          float maximumWidth,
                                          const TextWidthMeasurer& measureWidth)
{
    auto pieces = std::vector<std::string> {};
    auto current = std::string {};

    for (std::size_t index = 0; index < word.size(); ++index)
    {
        auto character = word[index];
        auto isNewCharacter = !isUtf8ContinuationByte(character);

        if (isNewCharacter && !current.empty() && measureWidth(current + character) > maximumWidth)
        {
            pieces.push_back(current);
            current.clear();
        }

        current += character;
    }

    if (!current.empty())
        pieces.push_back(current);

    return pieces;
}

std::string appendEllipsisWithinWidth(std::string line,
                                      float maximumWidth,
                                      const TextWidthMeasurer& measureWidth)
{
    while (!line.empty()
           && measureWidth(line + std::string {ellipsis}) > maximumWidth)
    {
        removeLastCharacter(line);
    }

    return line + std::string {ellipsis};
}
} // namespace

std::vector<std::string> wrapTextToWidth(std::string_view text,
                                         float maximumWidth,
                                         int maximumLineCount,
                                         const TextWidthMeasurer& measureWidth)
{
    auto lines = std::vector<std::string> {};
    auto current = std::string {};
    auto wasTruncated = false;

    auto startNewLine = [&](const std::string& firstPiece)
    {
        lines.push_back(current);
        current = firstPiece;
        wasTruncated = std::ssize(lines) == maximumLineCount;
    };

    for (const auto& word: splitIntoWords(text))
    {
        auto pieces = splitWordToWidth(word, maximumWidth, measureWidth);
        auto candidate = current.empty() ? pieces.front() : current + " " + pieces.front();

        if (current.empty() || measureWidth(candidate) <= maximumWidth)
            current = candidate;
        else
            startNewLine(pieces.front());

        for (std::size_t index = 1; index < pieces.size() && !wasTruncated; ++index)
            startNewLine(pieces[index]);

        if (wasTruncated)
            break;
    }

    if (!wasTruncated && !current.empty())
        lines.push_back(current);

    if (wasTruncated && !lines.empty())
        lines.back() = appendEllipsisWithinWidth(lines.back(), maximumWidth, measureWidth);

    return lines;
}
} // namespace term
