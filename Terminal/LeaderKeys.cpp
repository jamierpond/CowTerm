#include "LeaderKeys.h"

#include <string_view>

namespace term
{
namespace
{
constexpr auto focusDirections = std::string_view {"hjkl"};
constexpr auto resizeKeys = std::string_view {"HJKL"};

std::optional<char> findSingleCharacterIn(const std::string& characters,
                                          std::string_view allowed)
{
    if (characters.size() != 1)
        return std::nullopt;

    const auto position = allowed.find(characters[0]);

    if (position == std::string_view::npos)
        return std::nullopt;

    return allowed[position];
}
} // namespace

std::optional<char> findPaneResizeDirection(const std::string& characters)
{
    const auto key = findSingleCharacterIn(characters, resizeKeys);

    if (!key)
        return std::nullopt;

    return focusDirections[resizeKeys.find(*key)];
}

std::optional<char> findPaneFocusDirection(const std::string& characters)
{
    return findSingleCharacterIn(characters, focusDirections);
}

std::optional<int> findSessionIndexForDigit(const std::string& characters)
{
    const auto digit = findSingleCharacterIn(characters, "123456789");

    if (!digit)
        return std::nullopt;

    return *digit - '1';
}
} // namespace term
