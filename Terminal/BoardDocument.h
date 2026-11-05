#pragma once

#include "Zones.h"

#include <Miro/Reflect.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace term
{
// One card's stored state, keyed `<directory>@<branch>` (makeCardKey).
struct CardRecord
{
    std::string zone;

    MIRO_REFLECT(zone)
};

// The `board` document in the emberstore database: every zone a card was
// moved to, by Jamie on the board or by its agent with `ct board move`.
struct BoardDocument
{
    std::map<std::string, CardRecord> cards;

    MIRO_REFLECT(cards)
};

std::optional<Zone> getStoredZone(const BoardDocument& document, std::string_view key);

enum class MoveRequester
{
    jamie,
    agent
};

enum class MoveOutcome
{
    moved,
    unchanged,
    refusedForAgent
};

// An agent may only move its own card to the zones isZoneAnAgentMayMoveItselfTo allows.
MoveOutcome moveCard(BoardDocument& document,
                     const std::string& key,
                     Zone zone,
                     MoveRequester requester);
} // namespace term
