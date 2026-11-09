#pragma once

#include <string_view>

namespace term
{
// Whether next belongs to the grapheme cluster ending the text so far, by a
// reduced set of Unicode's break rules: combining marks, variation
// selectors, joiners and tags extend a cluster; a wide character after a
// zero-width joiner joins it (ZWJ emoji); an emoji skin-tone modifier joins; and two
// regional indicators make one flag.
bool doesExtendCluster(std::u32string_view cluster, char32_t next);

// True for the codepoints that can extend a cluster whatever precedes them,
// so a caller can skip the lookup for ordinary text.
bool isClusterExtender(char32_t codepoint);

inline constexpr char32_t variationSelectorEmoji = 0xfe0f;
} // namespace term
