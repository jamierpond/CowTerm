#pragma once

#include "Config.h"

#include <string>

namespace term
{
// This machine's name without its domain ("daily-driver" for
// daily-driver.local), or empty when the system will not say.
std::string getShortLocalHostName();

// This machine's name as the phone dials it: the config's hostName, or the
// short host name when the config sets none.
std::string getThisMachineName(const AppConfig& config);
} // namespace term
