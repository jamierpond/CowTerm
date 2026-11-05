#include "BoardDocument.h"

namespace term
{
std::optional<Zone> getStoredZone(const BoardDocument& document, std::string_view key)
{
    auto found = document.cards.find(std::string {key});

    if (found == document.cards.end())
        return std::nullopt;

    return findZoneByName(found->second.zone);
}

MoveOutcome moveCard(BoardDocument& document,
                     const std::string& key,
                     Zone zone,
                     MoveRequester requester)
{
    if (requester == MoveRequester::agent && !isZoneAnAgentMayMoveItselfTo(zone))
        return MoveOutcome::refusedForAgent;

    auto& card = document.cards[key];

    if (findZoneByName(card.zone) == zone)
        return MoveOutcome::unchanged;

    card.zone = std::string {getZoneName(zone)};
    return MoveOutcome::moved;
}
} // namespace term
