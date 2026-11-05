#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace term
{
// The board's four columns, in display order.
enum class Zone
{
    needsYou,
    working,
    review,
    done
};

const std::vector<Zone>& getColumnZonesInDisplayOrder();
std::string_view getZoneName(Zone zone);
std::optional<Zone> findZoneByName(std::string_view name);

// The letter after `m` that moves a card to the zone; only working, review and done have one.
std::optional<Zone> findZoneByMoveLetter(char letter);

// What the agent is told when Jamie moves its card here, starting "Jamie moved
// this to <zone>."; empty for zones that hold no agent yet.
std::string getZoneMessageForAgent(Zone zone);

bool isZoneAnAgentMayMoveItselfTo(Zone zone);

// Jamie moves cards to the zones that have a move letter; needs you is claude's call.
bool isZoneJamieMayMoveTo(Zone zone);
} // namespace term
