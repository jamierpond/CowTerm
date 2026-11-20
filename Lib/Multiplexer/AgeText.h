#pragma once

#include <cstdint>
#include <string>

namespace term
{
// "12 s ago", "4 min ago", "3 h ago", "2 d ago"; "just now" under a second.
std::string describeStoredScreenAge(std::int64_t ageMilliseconds);

// When a session was last used: "just now" under a minute, "yesterday" from
// one day to two, else as describeStoredScreenAge ("2 min ago", "3 h ago", "4 d ago").
std::string describeLastUseAge(std::int64_t ageMilliseconds);

// A span of time in its largest whole unit, spelled out: "3 days", "1 hour",
// "12 minutes"; "less than a minute" under one.
std::string describeAgeAsDuration(std::int64_t ageMilliseconds);
} // namespace term
