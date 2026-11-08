#pragma once

#include <optional>
#include <string>

namespace term
{
// fzf-style case-insensitive subsequence match (same scorer as Grazer's
// palette). Consecutive matches and word-boundary hits score higher; skipped
// text costs a little. Spaces in the query are ignored so "eacp term" can
// match "eacp — Terminal".
std::optional<int> fuzzyScore(const std::string& query, const std::string& text);
} // namespace term
