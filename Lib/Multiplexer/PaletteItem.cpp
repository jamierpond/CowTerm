#include "PaletteItem.h"

#include "AgeText.h"

namespace term
{
std::string getPaletteItemIcon(const PaletteItem& item)
{
    if (item.claude)
        return "✳";

    if (item.kind == PaletteItem::Kind::Session)
        return "●";

    return "▸";
}

std::string describePaletteItemAge(const PaletteItem& item, std::int64_t nowMilliseconds)
{
    if (item.kind != PaletteItem::Kind::Session || item.lastUsed == 0)
        return {};

    return describeLastUseAge(nowMilliseconds - item.lastUsed);
}
} // namespace term
