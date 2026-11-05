#include "Zones.h"

#include <algorithm>
#include <array>

#include <eacp/Core/Utils/Strings.h>

namespace term
{
namespace
{
struct ZoneDescription
{
    Zone zone;
    std::string_view name;
    char moveLetter;
    std::string_view messageForAgent;
    bool agentMayMoveItself;
};

constexpr auto noMoveLetter = '\0';

constexpr auto zoneDescriptions = std::array {
    ZoneDescription {Zone::needsYou, "needs you", noMoveLetter, "", false},
    ZoneDescription {Zone::working, "working", 'w', "Go: carry on with the brief.", false},
    ZoneDescription {
        Zone::review, "review", 'r', "Stop editing; Jamie is reviewing your diff in dv.", true},
    ZoneDescription {Zone::done, "done", 'd', "Accepted. Stop; make no more changes.", true},
};

const ZoneDescription& describeZone(Zone zone)
{
    return *std::ranges::find(zoneDescriptions, zone, &ZoneDescription::zone);
}
} // namespace

const std::vector<Zone>& getColumnZonesInDisplayOrder()
{
    static const auto zones = []
    {
        auto ordered = std::vector<Zone> {};

        for (const auto& description: zoneDescriptions)
            ordered.push_back(description.zone);

        return ordered;
    }();

    return zones;
}

std::string_view getZoneName(Zone zone)
{
    return describeZone(zone).name;
}

std::optional<Zone> findZoneByName(std::string_view name)
{
    auto found = std::ranges::find(zoneDescriptions, name, &ZoneDescription::name);

    if (found == zoneDescriptions.end())
        return std::nullopt;

    return found->zone;
}

std::optional<Zone> findZoneByMoveLetter(char letter)
{
    if (letter == noMoveLetter)
        return std::nullopt;

    auto found = std::ranges::find(zoneDescriptions, letter, &ZoneDescription::moveLetter);

    if (found == zoneDescriptions.end())
        return std::nullopt;

    return found->zone;
}

std::string getZoneMessageForAgent(Zone zone)
{
    const auto& description = describeZone(zone);

    if (description.messageForAgent.empty())
        return {};

    return eacp::Strings::concat(
        "Jamie moved this to ", description.name, ". ", description.messageForAgent);
}

bool isZoneJamieMayMoveTo(Zone zone)
{
    return describeZone(zone).moveLetter != noMoveLetter;
}

bool isZoneAnAgentMayMoveItselfTo(Zone zone)
{
    return describeZone(zone).agentMayMoveItself;
}
} // namespace term
