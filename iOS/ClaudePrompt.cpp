#include "ClaudePrompt.h"

#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <cctype>

namespace cowterm::mobile
{
namespace
{
constexpr auto boxSide = std::string_view {"\xe2\x94\x82"};
constexpr auto boxDrawingLowerHalf = std::string_view {"\xe2\x94"};
constexpr auto boxDrawingUpperHalf = std::string_view {"\xe2\x95"};
constexpr auto selectionMarker = std::string_view {"\xe2\x9d\xaf"};
constexpr auto whitespace = std::string_view {" \t"};

// Lines above the question that belong to the prompt: the tool and the
// command it asks about.
constexpr auto linesAboveQuestionInIdentity = 4;
constexpr auto fewestOptions = 2;

std::string_view trimWhitespace(std::string_view text)
{
    const auto first = text.find_first_not_of(whitespace);

    if (first == std::string_view::npos)
        return {};

    const auto last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

std::string_view removePrefix(std::string_view text, std::string_view prefix)
{
    if (text.starts_with(prefix))
        return trimWhitespace(text.substr(prefix.size()));

    return text;
}

std::string_view removeSuffix(std::string_view text, std::string_view suffix)
{
    if (text.ends_with(suffix))
        return trimWhitespace(text.substr(0, text.size() - suffix.size()));

    return text;
}

// A row of the screen without Claude's box sides or the selection arrow.
std::string cleanLine(std::string_view line)
{
    auto text = trimWhitespace(line);
    text = removePrefix(text, boxSide);
    text = removeSuffix(text, boxSide);
    text = removePrefix(text, selectionMarker);
    return std::string {text};
}

// "2. Yes, and don't ask again" as an option; empty for any other line.
std::optional<PromptOption> parseOption(const std::string& line)
{
    const auto dot = line.find(". ");

    if (dot == std::string::npos || dot == 0)
        return std::nullopt;

    const auto isNumber = std::ranges::all_of(line.substr(0, dot),
                                              [](char c) { return std::isdigit((unsigned char) c); });

    if (!isNumber)
        return std::nullopt;

    return PromptOption {std::stoi(line.substr(0, dot)), line.substr(dot + 2)};
}

bool isQuestion(const std::string& line)
{
    return !line.empty() && line.back() == '?';
}

// Claude's box: its sides and corners are in Unicode's box drawing block,
// U+2500 to U+257F, whose UTF-8 starts with one of these two pairs of bytes.
bool startsWithBoxDrawing(std::string_view line)
{
    return line.starts_with(boxDrawingLowerHalf) || line.starts_with(boxDrawingUpperHalf);
}

// Every line of a prompt is indented inside Claude's box; a row that starts
// at the first column is the rest of an option the screen's width cut short.
bool isWrappedContinuation(const std::string& rawLine)
{
    if (rawLine.empty() || rawLine.find_first_of(whitespace) == 0)
        return false;

    return !startsWithBoxDrawing(rawLine);
}

std::vector<PromptOption> collectOptionsAfter(const std::vector<std::string>& lines,
                                              const std::vector<std::string>& rawLines,
                                              std::size_t questionIndex)
{
    auto options = std::vector<PromptOption> {};

    for (auto index = questionIndex + 1; index < lines.size(); ++index)
    {
        if (!options.empty() && isWrappedContinuation(rawLines[index]))
        {
            options.back().label += lines[index];
            continue;
        }

        const auto option = parseOption(lines[index]);

        if (!option)
            break;

        options.push_back(*option);
    }

    return options;
}

// The lines above the question, the question, and the options as read, so a
// prompt reads the same however the screen's width wrapped it.
std::string joinIdentity(const std::vector<std::string>& lines,
                         std::size_t questionIndex,
                         const std::vector<PromptOption>& options)
{
    const auto first = questionIndex - std::min<std::size_t>(questionIndex, linesAboveQuestionInIdentity);
    auto identity = std::string {};

    for (auto index = first; index <= questionIndex; ++index)
        identity += lines[index] + "\n";

    for (const auto& option: options)
        identity += std::to_string(option.number) + ". " + option.label + "\n";

    return identity;
}
} // namespace

std::optional<PermissionPrompt> findPermissionPrompt(std::string_view screenText)
{
    const auto rawLines = eacp::Strings::split(screenText, '\n').getVector();
    auto lines = std::vector<std::string> {};

    for (const auto& line: rawLines)
        lines.push_back(cleanLine(line));

    // The last prompt on screen is the one waiting for an answer.
    for (auto index = lines.size(); index-- > 0;)
    {
        if (!isQuestion(lines[index]))
            continue;

        auto options = collectOptionsAfter(lines, rawLines, index);

        if ((int) options.size() < fewestOptions)
            continue;

        auto prompt = PermissionPrompt {};
        prompt.question = lines[index];
        prompt.identity = joinIdentity(lines, index, options);
        prompt.options = std::move(options);
        return prompt;
    }

    return std::nullopt;
}

std::optional<std::string> chooseKeysForTappedOption(const PermissionPrompt& drawnFor,
                                                     int optionNumber,
                                                     const std::optional<PermissionPrompt>& onScreen)
{
    if (!onScreen || onScreen->identity != drawnFor.identity)
        return std::nullopt;

    return std::to_string(optionNumber);
}
} // namespace cowterm::mobile
