#pragma once

#include <optional>
#include <string>

namespace term
{
// h/j/k/l after the leader: the direction to move focus toward.
std::optional<char> findPaneFocusDirection(const std::string& characters);

// 1..9 after the leader (or with Cmd): the zero-based session index.
std::optional<int> findSessionIndexForDigit(const std::string& characters);
} // namespace term
