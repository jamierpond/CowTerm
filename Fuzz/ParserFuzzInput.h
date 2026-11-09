#pragma once

#include <CowTermCore/TermParser.h>
#include <CowTermCore/TermScreen.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace term::fuzz
{
// A fuzz input is four size bytes (columns, rows, then the columns and rows
// to resize to halfway through) followed by the bytes a program wrote.
constexpr auto headerSize = std::size_t {4};
constexpr auto smallestDimension = 1;

struct ParserFuzzInput
{
    int columns = smallestDimension;
    int rows = smallestDimension;
    int resizedColumns = smallestDimension;
    int resizedRows = smallestDimension;
    std::string_view firstHalf;
    std::string_view secondHalf;
};

inline int readDimension(std::uint8_t byte)
{
    return std::max(smallestDimension, (int) byte);
}

inline ParserFuzzInput splitParserFuzzInput(std::string_view bytes)
{
    auto input = ParserFuzzInput {};

    if (bytes.size() < headerSize)
        return input;

    input.columns = readDimension((std::uint8_t) bytes[0]);
    input.rows = readDimension((std::uint8_t) bytes[1]);
    input.resizedColumns = readDimension((std::uint8_t) bytes[2]);
    input.resizedRows = readDimension((std::uint8_t) bytes[3]);

    const auto payload = bytes.substr(headerSize);
    const auto half = payload.size() / 2;
    input.firstHalf = payload.substr(0, half);
    input.secondHalf = payload.substr(half);
    return input;
}

// Renders the visible grid and scrollback as text, one line per row, so a
// replay can be compared against a recorded snapshot.
inline std::string describeScreen(const TermScreen& screen, int cursorShape)
{
    auto text = std::string {};
    const auto appendNumber = [&](auto value)
    {
        text += std::to_string(value);
        text += ' ';
    };

    appendNumber(screen.cursor.x);
    appendNumber(screen.cursor.y);
    appendNumber(cursorShape);
    appendNumber(screen.scrollbackSize());
    text += '\n';

    for (auto offset = 0; offset <= screen.scrollbackSize(); offset += screen.rows())
    {
        for (auto row = 0; row < screen.rows(); ++row)
        {
            for (const auto& cell: screen.lineAt(row, offset))
            {
                appendNumber((std::uint32_t) cell.ch);
                appendNumber(cell.fg);
                appendNumber(cell.bg);
                appendNumber(cell.attrs);
            }

            text += '\n';
        }
    }

    return text;
}

// Feeds one fuzz input through a parser and screen, answering every query,
// and returns the resulting screen as text.
inline std::string runParserFuzzInput(std::string_view bytes)
{
    const auto input = splitParserFuzzInput(bytes);
    const auto theme = Theme {};
    auto screen = TermScreen {input.columns, input.rows, theme};
    auto parser = TermParser {screen, theme};
    auto responses = std::string {};
    const auto recordEvent = [&](std::string_view kind, std::string_view text)
    {
        responses += kind;
        responses += text;
        responses += '\n';
    };

    parser.respond = [&](std::string_view reply) { recordEvent("reply ", reply); };
    parser.onTitleChanged = [&](const std::string& title) { recordEvent("title ", title); };
    parser.onCwdChanged = [&](const std::string& cwd) { recordEvent("cwd ", cwd); };
    parser.onNotify = [&](const std::string& text) { recordEvent("notify ", text); };
    parser.onBell = [&] { recordEvent("bell", {}); };

    parser.feed(input.firstHalf);
    screen.resize(input.resizedColumns, input.resizedRows);
    parser.feed(input.secondHalf);

    return describeScreen(screen, parser.cursorShape()) + responses;
}

// FNV-1a, so a snapshot is one number per recording rather than its whole grid.
inline std::uint64_t hashText(std::string_view text)
{
    constexpr auto offsetBasis = std::uint64_t {14695981039346656037ull};
    constexpr auto prime = std::uint64_t {1099511628211ull};
    auto hash = offsetBasis;

    for (const auto c: text)
    {
        hash ^= (std::uint8_t) c;
        hash *= prime;
    }

    return hash;
}
} // namespace term::fuzz
