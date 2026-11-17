#include "PaletteItem.h"

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
} // namespace term
