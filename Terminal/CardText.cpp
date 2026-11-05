#include "CardText.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace term
{
namespace
{
struct ColumnSynonym
{
    Zone zone;
    std::string_view badge;
};

constexpr auto columnSynonyms = std::array {
    ColumnSynonym {Zone::needsYou, "waiting"},
    ColumnSynonym {Zone::working, "busy"},
};

bool isBadgeRepeatingColumn(std::string_view badge, Zone zone)
{
    if (badge == getZoneName(zone))
        return true;

    return std::ranges::any_of(columnSynonyms,
                               [badge, zone](const ColumnSynonym& synonym)
                               { return synonym.zone == zone && synonym.badge == badge; });
}
} // namespace

std::string getShownCardDescription(const TaskCard& card)
{
    if (card.description == card.taskName)
        return {};

    return card.description;
}

std::string getShownCardBadge(const TaskCard& card)
{
    auto badge = card.badgeText;

    if (isBadgeRepeatingColumn(badge, card.zone))
        badge.clear();

    return badge;
}
} // namespace term
