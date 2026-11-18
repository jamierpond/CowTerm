#include "AgeText.h"

#include <array>
#include <string_view>

namespace term
{
namespace
{
struct AgeUnit
{
    std::int64_t milliseconds;
    std::string_view suffix;
};

constexpr auto millisecondsPerSecond = std::int64_t {1000};
constexpr auto millisecondsPerMinute = 60 * millisecondsPerSecond;
constexpr auto millisecondsPerHour = 60 * millisecondsPerMinute;
constexpr auto millisecondsPerDay = 24 * millisecondsPerHour;
constexpr auto oldestAgeCalledYesterday = 2 * millisecondsPerDay;

// Largest first: an age is told in the largest unit it has a whole one of.
constexpr auto ageUnits = std::array {
    AgeUnit {millisecondsPerDay, " d ago"},
    AgeUnit {millisecondsPerHour, " h ago"},
    AgeUnit {millisecondsPerMinute, " min ago"},
    AgeUnit {millisecondsPerSecond, " s ago"},
};
} // namespace

std::string describeStoredScreenAge(std::int64_t ageMilliseconds)
{
    for (const auto& unit: ageUnits)
    {
        if (ageMilliseconds >= unit.milliseconds)
            return std::to_string(ageMilliseconds / unit.milliseconds) + std::string {unit.suffix};
    }

    return "just now";
}

std::string describeLastUseAge(std::int64_t ageMilliseconds)
{
    if (ageMilliseconds < millisecondsPerMinute)
        return "just now";

    if (ageMilliseconds >= millisecondsPerDay && ageMilliseconds < oldestAgeCalledYesterday)
        return "yesterday";

    return describeStoredScreenAge(ageMilliseconds);
}
} // namespace term
