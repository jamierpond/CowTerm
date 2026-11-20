#pragma once

#include <CowTermPalette/PaletteItem.h>

#include <cstdint>
#include <optional>
#include <string>

namespace term
{
// The mark before a row's name: ✳ for Claude, ● for a session, ▸ for a project.
std::string getPaletteItemIcon(const PaletteItem& item);

// How long ago an open session was last used, worded as the phone's rows word
// it ("4 min ago"); empty for a project row and for a session never used.
std::string describePaletteItemAge(const PaletteItem& item, std::int64_t nowMilliseconds);
} // namespace term
