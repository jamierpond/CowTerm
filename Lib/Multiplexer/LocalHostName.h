#pragma once

#include <string>

namespace term
{
// This machine's name without its domain ("daily-driver" for
// daily-driver.local), or empty when the system will not say. Local sessions
// take this host's colour.
std::string getShortLocalHostName();
} // namespace term
