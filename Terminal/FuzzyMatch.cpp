#include "FuzzyMatch.h"

#include <algorithm>
#include <cctype>

namespace term
{
namespace
{
constexpr auto matchScore = 1;
constexpr auto consecutiveMatchScore = 8;
constexpr auto wordBoundaryBonus = 4;
constexpr auto maximumSkipPenalty = std::size_t {3};

char toLowerCase(char character)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
}

bool isWordBoundary(const std::string& text, std::size_t index)
{
    if (index == 0)
        return true;

    const auto previous = static_cast<unsigned char>(text[index - 1]);
    return !std::isalnum(previous);
}

std::optional<std::size_t> findNextMatch(const std::string& text,
                                         std::size_t position,
                                         char queryCharacter)
{
    const auto wanted = toLowerCase(queryCharacter);

    for (auto index = position; index < text.size(); ++index)
    {
        if (toLowerCase(text[index]) == wanted)
            return index;
    }

    return std::nullopt;
}

int scoreMatch(const std::string& text, std::size_t index, std::size_t position, bool consecutive)
{
    auto score = matchScore;

    if (consecutive && index == position)
        score = consecutiveMatchScore;

    if (isWordBoundary(text, index))
        score += wordBoundaryBonus;

    const auto skipped = std::min(index - position, maximumSkipPenalty);
    return score - static_cast<int>(skipped);
}
} // namespace

std::optional<int> fuzzyScore(const std::string& query, const std::string& text)
{
    auto score = 0;
    auto consecutive = false;
    auto position = std::size_t {0};

    for (const auto queryCharacter: query)
    {
        if (queryCharacter == ' ')
            continue;

        const auto index = findNextMatch(text, position, queryCharacter);

        if (!index)
            return std::nullopt;

        score += scoreMatch(text, *index, position, consecutive);
        position = *index + 1;
        consecutive = true;
    }

    return score;
}
} // namespace term
